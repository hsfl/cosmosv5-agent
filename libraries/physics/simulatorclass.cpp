#include "simulatorclass.h"
#include "support/jsonclass.h"
#include "support/stringlib.h"
#include <set>

namespace Cosmos
{
namespace Physics
{

// ---------------------------------------------------------------------------
// Sun-Synchronous Orbit (SSO) helpers — internal to simulatorclass
// ---------------------------------------------------------------------------
namespace
{
    // Physical constants
    static const double SSO_RE  = 6378137.0;          // Earth equatorial radius (m)
    static const double SSO_J2  = 1.08262668e-3;      // J2 oblateness coefficient
    static const double SSO_GM  = 3.986004418e14;     // Earth gravitational parameter (m³/s²)
    // Rate at which the Sun's RA advances: one full circle per tropical year
    static const double SSO_SUN_RATE = D2PI / (365.25 * 86400.); // rad/s

    /**
     * \brief Required inclination for a circular SSO at the given altitude.
     *
     * Derived from the J2 nodal precession rate:
     *   dΩ/dt = -(3/2) n J2 (Re/a)² cos(i)
     * Setting dΩ/dt = SSO_SUN_RATE and solving for i.
     *
     * \param alt_m  Altitude above mean Earth surface (metres)
     * \return Inclination in radians (retrograde, > π/2, for typical LEO)
     */
    double sso_inclination(double alt_m)
    {
        double a    = SSO_RE + alt_m;
        double cosi = -SSO_SUN_RATE * pow(a, 3.5)
                      / (1.5 * SSO_J2 * SSO_RE * SSO_RE * sqrt(SSO_GM));
        cosi = std::max(-1., std::min(1., cosi));   // guard extreme altitudes
        return acos(cosi);
    }

    /**
     * \brief Initial RAAN placing the ascending node at the desired LST.
     *
     * LST of the ascending node = GMST + RAAN (mod 2π), so
     *   RAAN = (lst_rad - GMST) + (sun_RA offset from noon crossing)
     *
     * Convention: LST 12:00 means the node is on the Sun-side (descending
     * passes at midnight).  LST 10:30 → dawn/dusk orbit.
     *
     * \param lst_hours  Desired local solar time of ascending node (hours, 0–24)
     * \param utc_mjd    Simulation epoch (MJD)
     * \return RAAN in radians [0, 2π)
     */
    double sso_raan(double lst_hours, double utc_mjd)
    {
        // Greenwich Mean Sidereal Time (radians) at epoch
        double T        = (utc_mjd - 51544.5) / 36525.;
        double gmst_deg = 280.46061837
                        + 360.98564736629 * (utc_mjd - 51544.5)
                        + 0.000387933 * T * T
                        - T * T * T / 38710000.;
        double gmst = fmod(gmst_deg * DPI / 180., D2PI);
        if (gmst < 0.) gmst += D2PI;

        // Sun's mean right ascension at epoch
        double sun_lon_deg = 280.460 + 0.9856474 * (utc_mjd - 51544.5);
        double sun_ra = fmod(sun_lon_deg * DPI / 180., D2PI);
        if (sun_ra < 0.) sun_ra += D2PI;

        // Offset: (lst - 12h) hours × 15°/h expressed in radians
        // LST 12h → node directly under the Sun (ascending through noon meridian)
        double raan = sun_ra + (lst_hours - 12.) * (DPI / 12.);
        raan = fmod(raan, D2PI);
        if (raan < 0.) raan += D2PI;
        return raan;
    }
} // anonymous namespace
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Frozen Lunar Orbit (FLO) helpers — internal to simulatorclass
// ---------------------------------------------------------------------------
namespace
{
    // Lunar physical constants
    static const double FLO_RM   = 1737400.0;       // Moon mean radius (m)
    static const double FLO_GM   = 4.9048695e12;    // Moon gravitational parameter (m³/s²)

    // Moon's J2 and C22 (LP150Q model, widely used for frozen-orbit design)
    static const double FLO_J2   = 2.0330e-4;
    static const double FLO_C22  = 2.2372e-5;

    // The critical inclination nearest 90° for a polar frozen orbit.
    // For the Moon the frozen eccentricity vector combination of J2 and C22
    // yields a stable frozen condition at i ≈ 90° ± a small offset driven by
    // C22.  For a purely polar orbit we use exactly 90°; callers wanting the
    // analytically corrected value can use flo_frozen_inclination() below.
    //
    // Frozen eccentricity at altitude h (circular seed):
    //   e_frozen ≈ (5/2) * (J2/2 + C22) * (RM/a)^2 * cos(i)
    // At i = 90° this vanishes, so e ≈ 0 and the orbit is naturally frozen.
    // For i slightly off 90° a small eccentricity corrects it; we carry that
    // calculation so the caller can start from the true frozen state.

    /**
     * \brief Frozen eccentricity magnitude for a lunar orbit.
     *
     * Uses the first-order secular frozen-orbit condition:
     *   e_f = (5/4) * (RM/a)^2 * (J2 - 5*C22*cos(2*w)) * cos(i) / sin^2(i)
     *
     * For a polar orbit (i = 90°) e_f = 0 and the argument of perigee is
     * unconstrained.  For i ≠ 90° a non-zero eccentricity is needed.  The
     * stable frozen argument of perigee is ω = 90° or 270°.
     *
     * Reference: Abad, San-Juan & Gavín (2009), "Near Circular Frozen Orbits
     * for the Dopri-CW System of Equations."
     *
     * \param alt_m  Altitude above mean Moon surface (metres)
     * \param incl   Inclination (radians)
     * \return       Frozen eccentricity (dimensionless, ≥ 0)
     */
    double flo_frozen_eccentricity(double alt_m, double incl)
    {
        double a      = FLO_RM + alt_m;
        double ratio2 = (FLO_RM / a) * (FLO_RM / a);
        double ci     = cos(incl);
        double si     = sin(incl);
        if (fabs(si) < 1e-6) return 0.;          // equatorial — no frozen solution
        // Frozen ω = 90° ⟹ cos(2ω) = -1
        double ef = (5. / 4.) * ratio2 * (FLO_J2 - 5. * FLO_C22 * (-1.)) * ci / (si * si);
        return fabs(ef);
    }

    /**
     * \brief Circular (seed) orbital speed for a lunar orbit.
     * \param alt_m  Altitude above mean Moon surface (metres)
     * \return       Speed in m/s
     */
    double flo_circular_speed(double alt_m)
    {
        double a = FLO_RM + alt_m;
        return sqrt(FLO_GM / a);
    }

    /**
     * \brief Orbital period for a lunar orbit.
     * \param alt_m  Altitude above mean Moon surface (metres)
     * \return       Period in seconds
     */
    double flo_period(double alt_m)
    {
        double a = FLO_RM + alt_m;
        return D2PI * sqrt(a * a * a / FLO_GM);
    }

    /**
     * \brief Nodal precession rate (dΩ/dt) for a frozen lunar orbit.
     *
     * First-order J2 secular rate:
     *   dΩ/dt = -(3/2) n J2 (RM/a)² cos(i) / (1-e²)²
     *
     * For a frozen orbit e is small, so (1-e²)² ≈ 1.
     *
     * \param alt_m  Altitude (m)
     * \param incl   Inclination (rad)
     * \return       dΩ/dt in rad/s  (negative = westward for prograde)
     */
    double flo_raan_rate(double alt_m, double incl)
    {
        double a = FLO_RM + alt_m;
        double n = sqrt(FLO_GM / (a * a * a));
        return -1.5 * n * FLO_J2 * (FLO_RM / a) * (FLO_RM / a) * cos(incl);
    }

    /**
     * \brief Argument-of-perigee drift rate (dω/dt) for a frozen lunar orbit.
     *
     * First-order J2 secular rate:
     *   dω/dt = (3/4) n J2 (RM/a)² (5 cos²i - 1) / (1-e²)²
     *
     * At the frozen condition ω is stationary (dω/dt = 0) for the full
     * model; here we return the J2-only term so callers can verify.
     * For a polar orbit (i = 90°, cos²i = 0): dω/dt = -(3/4) n J2 (RM/a)²
     *
     * \param alt_m  Altitude (m)
     * \param incl   Inclination (rad)
     * \return       dω/dt in rad/s
     */
    double flo_aop_rate(double alt_m, double incl)
    {
        double a  = FLO_RM + alt_m;
        double n  = sqrt(FLO_GM / (a * a * a));
        double ci = cos(incl);
        return 0.75 * n * FLO_J2 * (FLO_RM / a) * (FLO_RM / a) * (5. * ci * ci - 1.);
    }
} // anonymous namespace (FLO)
// ---------------------------------------------------------------------------

//! Start time for an orbit whose own epoch is not absolute: the Init() base if that is
//! already an MJD (> 3600), otherwise the base as an offset from now.
static double epoch_base(double base)
{
    return base > 3600. ? base : base + currentmjd();
}

int32_t Simulator::Init(double idt, string realm, double iutc)
{
    realmname = realm;
    initialutc = iutc;
    dt = idt;
    dtj = dt / 86400.;
    //    if (initialutc > 3600.)
    //    {
    //        currentutc = initialutc;
    //        offsetutc = initialutc - currentmjd();
    //        dt = 86400.*((initialutc + (dt / 86400.))-initialutc);
    //        dtj = dt / 86400.;
    //    }

    RunState = State::Paused;
    if (server)
    {

    }
    error = 0;
    return error;
}

bool compareByPriority(const Physics::State* left, const Physics::State* right)
{
    return left->propagation_priority < right->propagation_priority;
}

//! \brief Add a detector to the list of available detectors in the Sim.
//! \param fov Full Field of View of the detector in radians.
//! \param ifov Instrument FOV (single pixel) in radians.
//! \param specmin Lower wavelength of spectral coverage, in meters.
//! \param specmax Upper wavelength of spectral coverage, in meters.
//! \return New number of detectors, or negative error.
int32_t Simulator::AddDetector(string name, float fov, float ifov, float specmin, float specmax)
{
    camstruc det;
    det.name = name;
    det.fov = fov;
    det.ifov = ifov;
    det.specmin = specmin;
    det.specmax = specmax;
    det.state = 0;
    return AddDetector(det);
}

int32_t Simulator::AddDetector(camstruc& det)
{
    if (detectors.find(det.name) == detectors.end())
    {
        detectors[det.name] = det;
    }
    return detectors.size();

}

//! \brief Add a target to the list of available targets in the Sim.
//! \param name Unique target name.
//! \param loc Location as ::Convert::locstruc.
//! \param type Node type.
//! \param size Size as ::gvector.
//! \return Current size of target list, or negative error.

int32_t Simulator::AddTarget(std::string name, locstruc loc, NODE_TYPE type, gvector size)
{
    targetstruc ttarget;
    ttarget.type = type;
    ttarget.name = name;
    //    ttarget.cloc = loc;
    ttarget.area = 0.;
    ttarget.size = size;
    ttarget.loc = loc;

    return AddTarget(ttarget);
}

//! \brief Add a target to the list of available targets in the Sim.
//! \param name Unique target name.
//! \param loc Location as ::Convert::locstruc.
//! \param type Node type.
//! \param area Area of target, in square meters.
//! \return Current size of target list, or negative error.

int32_t Simulator::AddTarget(std::string name, locstruc loc, NODE_TYPE type, double area)
{
    targetstruc ttarget;
    ttarget.type = type;
    ttarget.name = name;
    //    ttarget.cloc = loc;
    ttarget.size = gvector();
    ttarget.area  = area;
    ttarget.loc = loc;

    return AddTarget(ttarget);
}

//! \brief Add a target to the list of available targets in the Sim.
//! \param name Unique target name.
//! \param lat Central latitude of target, in radians.
//! \param lon Central longitude of target, in radians.
//! \param alt Central altitude of target, in meters.
//! \param type Node type.
//! \return Current size of target list, or negative error.

int32_t Simulator::AddTarget(string name, double lat, double lon, double alt, NODE_TYPE type)
{
    return AddTarget(name, lat, lon, DPI * 1e6, alt, type);
}

//! \brief Add a target to the list of available targets in the Sim.
//! \param name Unique target name.
//! \param lat Central latitude of target, in radians.
//! \param lon Central longitude of target, in radians.
//! \param alt Central altitude of target, in meters.
//! \param area Area of target, in square meters.
//! \param type Node type.
//! \return Current size of target list, or negative error.

int32_t Simulator::AddTarget(string name, double lat, double lon, double area, double alt, NODE_TYPE type)
{
    locstruc loc;
    loc.pos.geod.utc = currentutc;
    loc.pos.geod.s.lat = lat;
    loc.pos.geod.s.lon = lon;
    loc.pos.geod.s.h = alt;
    loc.pos.geod.v = gv_zero();
    loc.pos.geod.a = gv_zero();
    pos_set_geod(loc);
    return AddTarget(name, loc, type, area);
}

//! \brief Add a target to the list of available targets in the Sim.
//! \param name Unique target name.
//! \param ullat Upper left latitude of target area.
//! \param ullon Upper left longitude of target area.
//! \param lrlat Lower right latitude of target area.
//! \param lrlon Lower right longitude of target area.
//! \param type Node type.
//! \return Current size of target list, or negative error.

int32_t Simulator::AddTarget(string name, double ullat, double ullon, double lrlat, double lrlon, double alt, NODE_TYPE type)
{
    locstruc loc;
    loc.pos.geod.utc = currentutc;
    loc.pos.geod.s.lat = (ullat + lrlat) / 2.;
    loc.pos.geod.s.lon = (ullon + lrlon) / 2.;
    loc.pos.geod.s.h = alt;
    loc.pos.geod.v = gv_zero();
    loc.pos.geod.a = gv_zero();
    pos_set_geod(loc);
    double area = (ullat-lrlat) * (cos(lrlon) * lrlon - cos(ullon) * ullon) * REARTHM * REARTHM;
    return AddTarget(name, loc, type, area);
}

//! \brief Add a target to the list of available targets in the Sim.
//! \param targ Complete target as ::targetstruc.
//! \return Current size of target list, or negative error.

int32_t Simulator::AddTarget(targetstruc& targ)
{
    if (targets.find(targ.name) == targets.end())
    {
        targets[targ.name] = targ;
        for (auto &state : cnodes)
        {
            state->currentinfo.target.push_back(targ);
            ++state->currentinfo.target_cnt;
#ifndef INFLIGHT_BUILD
            state->currentinfo.add_target_names();
#endif
        }
    }
    return targets.size();
}

//! \brief Add orbit from line of JSON in a file.
//! \param filename Path to file containing satellite information.
//! \return Number of arguments, or negative error.

int32_t Simulator::ParseOrbitFile(string filename)
{
    int32_t iretn = 0;
    string  line;
    FILE *fp = nullptr;
    if (filename.empty())
    {
        filename = get_realmdir(realmname, true) + "/" + "orbit.dat";
        fp = fopen(filename.c_str(), "r");
        if (fp == nullptr)
        {
            return -1;
        }
    }
    else
    {
        fp = fopen(filename.c_str(), "r");
        if (fp == nullptr)
        {
            filename = get_realmdir(realmname, true) + "/" + filename;
            fp = fopen(filename.c_str(), "r");
            if (fp == nullptr)
            {
                return -1;
            }
        }
    }

    if (fp != nullptr)
    {
        line.resize(1010);
        while (fgets((char *)line.data(), 1000, fp) != nullptr)
        {
            if (line.front() == '#') { continue; }
            iretn = ParseOrbitString(line);
            if (iretn < 0)
            {
                fclose(fp);
                return iretn;
            }
        }
        fclose(fp);
    } else {
        return -1;
    }
    return iretn;
}

//! \brief Add orbit from line of JSON in a string.
//! \param args JSON line of orbit arguments.
//! \return Number of arguments, or negative error.

int32_t Simulator::ParseOrbitString(string args)
{
    double deltautc = 0.;
    uint16_t argcount = 0;
    string estring;
    json11::Json jargs = json11::Json::parse(args, estring);
    if (!jargs["model"].is_null())
    {
        ++argcount;
        if (jargs["model"].string_value() == "fast")
        {
            fastcalc = true;
        }
        else if (jargs["model"].string_value() == "slow")
        {
            fastcalc = false;
        }
    }
    // JIMNOTE: this is the path
    if (!jargs["phys"].is_null())
    {
        ++argcount;
        json11::Json::object values = jargs["phys"].object_items();
        if (values["utc"].number_value() != 0.)
        {
            if (values["utc"].number_value() > 3600.)
            {
                initialutc = values["utc"].number_value();
            }
            else
            {
                initialutc += values["utc"].number_value();
            }
        }
        else
        {
            initialutc = epoch_base(initialutc);
        }
        currentutc = initialutc;
        offsetutc = initialutc - currentmjd();
        dt = 86400.*((initialutc + (dt / 86400.))-initialutc);
        dtj = dt / 86400.;
        if (deltautc != 0.)
        {
            endutc = initialutc + deltautc;
        }
        double initiallat = RADOF(21.3069);
        double initiallon = RADOF(-157.8583);
        double initialalt = 400000.;
        double initialangle = RADOF(54.);
        // JIMNOTE: need checks for existence?
        initiallat = RADOF(values["lat"].number_value());
        initiallon = RADOF(values["lon"].number_value());
        initialalt = values["alt"].number_value();
        initialangle = RADOF(values["angle"].number_value());
        initialloc = Physics::shape2eci(initialutc, initiallat, initiallon, initialalt, initialangle, 0.);
    }
    if (!jargs["eci"].is_null())
    {
        ++argcount;
        json11::Json::object values = jargs["eci"].object_items();
        initialloc.pos.eci.utc = (values["utc"].number_value());
        initialloc.pos.eci.s.col[0] = (values["x"].number_value());
        initialloc.pos.eci.s.col[1] = (values["y"].number_value());
        initialloc.pos.eci.s.col[2] = (values["z"].number_value());
        initialloc.pos.eci.v.col[0] = (values["vx"].number_value());
        initialloc.pos.eci.v.col[1] = (values["vy"].number_value());
        initialloc.pos.eci.v.col[2] = (values["vz"].number_value());
        initialloc.pos.eci.a.col[0] = (values["ax"].number_value());
        initialloc.pos.eci.a.col[1] = (values["ay"].number_value());
        initialloc.pos.eci.a.col[2] = (values["az"].number_value());
        initialutc = initialloc.pos.eci.utc;
        currentutc = initialutc;
        offsetutc = initialutc - currentmjd();
        dt = 86400.*((initialutc + (dt / 86400.))-initialutc);
        dtj = dt / 86400.;
        if (deltautc != 0.)
        {
            endutc = initialutc + deltautc;
        }
    }
    if (!jargs["kep"].is_null())
    {
        ++argcount;
        json11::Json::object values = jargs["kep"].object_items();
        if (values["utc"].number_value() != 0.)
        {
            initialutc += values["utc"].number_value();
        }
        else
        {
            initialutc = epoch_base(initialutc);
        }
        currentutc = initialutc;
        offsetutc = initialutc - currentmjd();
        dt = 86400.*((initialutc + (dt / 86400.))-initialutc);
        dtj = dt / 86400.;
        if (deltautc != 0.)
        {
            endutc = initialutc + deltautc;
        }
        kepstruc kep;
        kep.utc = initialutc;
        kep.ea = values["ea"].number_value();
        kep.i = values["i"].number_value();
        kep.ap = values["ap"].number_value();
        kep.raan = values["raan"].number_value();
        kep.e = values["e"].number_value();
        kep.a = values["a"].number_value();
        kep2eci(kep, initialloc.pos.eci);
    }
    if (!jargs["tle"].is_null())
    {
        ++argcount;
        json11::Json::object values = jargs["tle"].object_items();
        vector<Convert::tlestruc>lines;
        string fname = get_realmdir(realmname, true) + "/" + values["filename"].string_value();
        load_lines(fname, lines);
        if (fabs(initialutc) <= 3600.)
        {
            initialutc +=lines[0].utc;
        }
        currentutc = initialutc;
        offsetutc = initialutc - currentmjd();
        dt = 86400.*((initialutc + (dt / 86400.))-initialutc);
        dtj = dt / 86400.;
        if (deltautc != 0.)
        {
            endutc = initialutc + deltautc;
        }
        initialloc.tle = lines[0];
        tle2eci(initialutc, initialloc.tle, initialloc.pos.eci);
    }
    if (!jargs["sso"].is_null())
    {
        // Sun-Synchronous Orbit specified by altitude and local solar time.
        //
        // Required orbit.dat syntax (all fields mandatory):
        //   {"sso":{"alt":<metres>, "lst":<hours 0-24>, "utc":<mjd or 0>}}
        //
        // alt  — altitude above mean Earth surface in metres (e.g. 550000)
        // lst  — local solar time of the *ascending* node crossing in decimal
        //        hours (e.g. 10.5 for 10:30 AM, 22.5 for 10:30 PM)
        // utc  — epoch: absolute MJD if > 3600, offset from current time if
        //        ≤ 3600 (use 0 for "now")
        //
        // The inclination is derived analytically from the J2 precession
        // formula so the orbital plane precesses at exactly the Sun's mean
        // motion (~0.9856°/day).  The initial RAAN is computed from the
        // requested LST and the Sun's RA at the epoch.  The orbit is circular
        // and the satellite starts at the ascending node (true anomaly = 0).
        //
        // Per-step precession is applied inside Simulator::Propagate() using
        // the stored sso_raan_rate member, so the orbit file needs no further
        // attention after startup.
        ++argcount;
        json11::Json::object values = jargs["sso"].object_items();

        // --- epoch ---
        if (values["utc"].number_value() > 3600.)
        {
            initialutc = values["utc"].number_value();
        }
        else
        {
            initialutc = epoch_base(initialutc) + values["utc"].number_value();
        }
        currentutc = initialutc;
        offsetutc  = initialutc - currentmjd();
        dt  = 86400. * ((initialutc + (dt / 86400.)) - initialutc);
        dtj = dt / 86400.;
        if (deltautc != 0.)
        {
            endutc = initialutc + deltautc;
        }

        // --- derived orbital elements ---
        double alt_m    = values["alt"].number_value();
        double lst_hrs  = values["lst"].number_value();
        double incl     = sso_inclination(alt_m);
        double raan     = sso_raan(lst_hrs, initialutc);
        double a        = SSO_RE + alt_m;
        double v_circ   = sqrt(SSO_GM / a);       // circular speed (m/s)

        // --- build ECI state at ascending node (true anomaly = 0) ---
        // Position: a·[cos Ω, sin Ω, 0]
        initialloc.pos.eci.utc      = initialutc;
        initialloc.pos.eci.s.col[0] = a * cos(raan);
        initialloc.pos.eci.s.col[1] = a * sin(raan);
        initialloc.pos.eci.s.col[2] = 0.;
        // Velocity at ascending node: v·[−sin Ω cos i, cos Ω cos i, sin i]
        initialloc.pos.eci.v.col[0] = v_circ * (-sin(raan) * cos(incl));
        initialloc.pos.eci.v.col[1] = v_circ * ( cos(raan) * cos(incl));
        initialloc.pos.eci.v.col[2] = v_circ *   sin(incl);
        initialloc.pos.eci.a.col[0] = 0.;
        initialloc.pos.eci.a.col[1] = 0.;
        initialloc.pos.eci.a.col[2] = 0.;

        // --- store SSO precession rate for use in Propagate() ---
        // dΩ/dt = SSO_SUN_RATE (rad/s); convert to rad/step in Propagate().
        sso_enabled   = true;
        sso_alt_m     = alt_m;
        sso_raan_rate = SSO_SUN_RATE;   // rad/s; multiplied by dt in Propagate()
    }
    if (!jargs["flo"].is_null())
    {
        // Frozen Lunar Orbit — two input modes
        // ─────────────────────────────────────────────────────────────────────
        // MODE A  Simple altitude + inclination (eccentricity computed from J2/C22):
        //   {"flo":{"alt":<m>, "inc":<deg>, "raan":<deg>, "utc":<mjd|0>}}
        //
        //   alt  — altitude above mean Moon surface (m), e.g. 100000
        //   inc  — inclination (deg); 90° → polar, zero frozen eccentricity
        //   raan — RAAN (deg, Moon-centred J2000); default 0
        //   aop  — argument of perigee (deg); default 270° (south-pole perigee,
        //          the other stable frozen condition alongside 90°); set to 90°
        //          for north-pole perigee
        //   ta   — true anomaly at epoch (deg); default 0°
        //
        //   The frozen eccentricity and RAAN precession rate are derived
        //   analytically from the Moon's J2 and C22 gravity coefficients.
        //
        // MODE B  Full Keplerian elements (use when a mission report gives them
        //          explicitly, e.g. VMMO Phase B1):
        //   {"flo":{"a":<m>, "e":<–>, "inc":<deg>, "raan":<deg>,
        //            "aop":<deg>, "ta":<deg>, "utc":<mjd|0>}}
        //
        //   a    — semi-major axis (m), e.g. 1857600
        //   e    — eccentricity, e.g. 0.0428
        //   inc  — inclination (deg)
        //   raan — RAAN (deg)
        //   aop  — argument of perigee (deg)
        //   ta   — true anomaly at epoch (deg); default 0°
        //
        //   When "a" is present the code uses MODE B and ignores "alt".
        //   The eccentricity is taken as given — no analytical correction.
        //
        // Common to both modes
        // ─────────────────────────────────────────────────────────────────────
        // Secular RAAN precession (J2 dΩ/dt) is applied every step inside
        // Simulator::Propagate() via flo_raan_rate_rads.  The Moon's gravity
        // field is far lumpier than Earth's (large C22, mascons); for
        // mission-critical work use a full GLGM-3/LP150Q gravity model.  This
        // approximation is suitable for coverage studies and trade analysis.
        ++argcount;
        json11::Json::object values = jargs["flo"].object_items();

        // --- epoch ---
        if (values["utc"].number_value() > 3600.)
        {
            initialutc = values["utc"].number_value();
        }
        else
        {
            initialutc = epoch_base(initialutc) + values["utc"].number_value();
        }
        currentutc = initialutc;
        offsetutc  = initialutc - currentmjd();
        dt  = 86400. * ((initialutc + (dt / 86400.)) - initialutc);
        dtj = dt / 86400.;
        if (deltautc != 0.)
        {
            endutc = initialutc + deltautc;
        }

        // --- orbital elements (MODE B takes priority when "a"/"sma" is present,
        //     or when "e" is given explicitly alongside "alt") ---
        double a, e_f, incl, raan, aop, ta;

        incl = RADOF(values["inc"].number_value());         // always required
        raan = RADOF(values["raan"].number_value());        // default 0°
        // aop: default 270° (south-pole perigee, stable frozen condition)
        aop  = values["aop"].is_null()
               ? (3. * DPI / 2.)
               : RADOF(values["aop"].number_value());
        ta   = values["ta"].is_null()
               ? 0.
               : RADOF(values["ta"].number_value());

        // Resolve semi-major axis: accept "a" or "sma" as the key
        bool have_sma = !values["a"].is_null() || !values["sma"].is_null();
        bool have_ecc = !values["e"].is_null();

        if (have_sma)
        {
            // MODE B — full Keplerian elements supplied directly
            a   = values["a"].is_null()
                  ? values["sma"].number_value()
                  : values["a"].number_value();
            e_f = have_ecc ? values["e"].number_value() : 0.;
            flo_alt_m = a - FLO_RM;
//            std::fprintf(stderr, "[flo] Mode B: a=%.1f m  e=%.5f  i=%.2f°  "
//                         "raan=%.2f°  aop=%.2f°  ta=%.2f°\n",
//                         a, e_f, DEGOF(incl), DEGOF(raan), DEGOF(aop), DEGOF(ta));
        }
        else if (!values["alt"].is_null() && have_ecc)
        {
            // MODE B via alt+e — altitude and eccentricity given explicitly
            flo_alt_m = values["alt"].number_value();
            a         = FLO_RM + flo_alt_m;
            e_f       = values["e"].number_value();
//            std::fprintf(stderr, "[flo] Mode B (alt+e): alt=%.0f m  a=%.1f m  "
//                         "e=%.5f  i=%.2f°  raan=%.2f°  aop=%.2f°  ta=%.2f°\n",
//                         flo_alt_m, a, e_f, DEGOF(incl), DEGOF(raan), DEGOF(aop), DEGOF(ta));
        }
        else
        {
            // MODE A — altitude given; compute frozen eccentricity analytically
            flo_alt_m = values["alt"].number_value();
            a         = FLO_RM + flo_alt_m;
            e_f       = flo_frozen_eccentricity(flo_alt_m, incl);
//            std::fprintf(stderr, "[flo] Mode A: alt=%.0f m  a=%.1f m  "
//                         "e_frozen=%.5f  i=%.2f°\n",
//                         flo_alt_m, a, e_f, DEGOF(incl));
        }

        // ── Convert Keplerian elements to ECI Cartesian ──────────────────────
        //
        // For the given true anomaly ν:
        //   p   = a(1 − e²)                     semi-latus rectum
        //   r   = p / (1 + e cosν)               radius
        //   v_r = √(μ/p) · e sinν               radial speed
        //   v_t = √(μ/p) · (1 + e cosν)         transverse speed
        //
        // In the perifocal frame (P̂ toward perigee, Q̂ 90° ahead):
        //   r_pf = r · [cosν, sinν, 0]
        //   v_pf = √(μ/p) · [−sinν, e+cosν, 0]
        //
        // Rotate to ECI via R3(−Ω) · R1(−i) · R3(−ω):
        //   (standard 313 Euler rotation, angles negated for active rotation)

        double p     = a * (1. - e_f * e_f);
        double r     = p / (1. + e_f * cos(ta));
        double sqmup = sqrt(FLO_GM / p);

        // Perifocal position and velocity
        double rx_pf = r * cos(ta);
        double ry_pf = r * sin(ta);
        double vx_pf = sqmup * (-sin(ta));
        double vy_pf = sqmup * (e_f + cos(ta));

        // Rotation matrix elements: R = R3(−Ω) · R1(−i) · R3(−ω)
        // Following Bate, Mueller & White §2.6:
        //   Qxx = cos Ω cos ω − sin Ω sin ω cos i
        //   Qxy = −cos Ω sin ω − sin Ω cos ω cos i
        //   Qyx = sin Ω cos ω + cos Ω sin ω cos i
        //   Qyy = −sin Ω sin ω + cos Ω cos ω cos i
        //   Qzx = sin ω sin i
        //   Qzy = cos ω sin i
        double cO = cos(raan), sO = sin(raan);
        double cI = cos(incl), sI = sin(incl);
        double cw = cos(aop),  sw = sin(aop);

        double Qxx = cO*cw - sO*sw*cI;
        double Qxy = -cO*sw - sO*cw*cI;
        double Qyx = sO*cw + cO*sw*cI;
        double Qyy = -sO*sw + cO*cw*cI;
        double Qzx = sw*sI;
        double Qzy = cw*sI;

        // The Q rotation produced a vector in SELENOGRAPHIC axes (since
        // incl/raan/aop are measured relative to Moon's equator/pole).
        // Init() will copy pos.eci → pos.sci and call pos_sci(), so
        // pos.eci must hold the selenocentric vector in J2000-ALIGNED
        // (SCI) axes -- not selenographic body-fixed axes.
        // Apply s2j (selenographic → SCI) at initialutc to get there.
        pos_extra(initialutc, initialloc);   // populate s2j

        rvector selc_s, selc_v;
        selc_s.col[0] = Qxx*rx_pf + Qxy*ry_pf;
        selc_s.col[1] = Qyx*rx_pf + Qyy*ry_pf;
        selc_s.col[2] = Qzx*rx_pf + Qzy*ry_pf;
        selc_v.col[0] = Qxx*vx_pf + Qxy*vy_pf;
        selc_v.col[1] = Qyx*vx_pf + Qyy*vy_pf;
        selc_v.col[2] = Qzx*vx_pf + Qzy*vy_pf;

        initialloc.pos.eci.s   = rv_mmult(initialloc.pos.extra.s2j, selc_s);
        initialloc.pos.eci.v   = rv_mmult(initialloc.pos.extra.s2j, selc_v);
        initialloc.pos.eci.a   = rv_zero();
        initialloc.pos.eci.utc = initialutc;

//        std::fprintf(stderr, "[flo selg->sci] |eci.s|=%.1f km  "
//                     "eci.s=(%.1f,%.1f,%.1f)  eci.v=(%.3f,%.3f,%.3f)\n",
//                     length_rv(initialloc.pos.eci.s)/1000.,
//                     initialloc.pos.eci.s.col[0], initialloc.pos.eci.s.col[1], initialloc.pos.eci.s.col[2],
//                     initialloc.pos.eci.v.col[0], initialloc.pos.eci.v.col[1], initialloc.pos.eci.v.col[2]);
//        std::fflush(stderr);

        // --- store FLO parameters for use in Propagate() ---
        flo_enabled        = true;
        flo_incl_rad       = incl;
        flo_raan_rate_rads = flo_raan_rate(flo_alt_m, incl); // rad/s, negative = westward
        // Signal to ParseSatString / AddNode that this is a lunar orbit so
        // PositionLunar is used instead of the default Earth-centred propagator.
        flo_ptype = Propagator::Type::PositionLunar;
    }
    pos_set_eci(initialloc);
    if (initialloc.tle.utc == 0.)
    {
        eci2tle2(initialloc.pos.eci, initialloc.tle);
    }
    return argcount;
}

//! \brief Add satellites from lines of JSON in a file.
//! \param filename Path to file containing satellite information.
//! \return Number of satellites, or negative error.

int32_t Simulator::ParseSatFile(string filename)
{
    string  line;
    FILE *fp;
    int32_t iretn;

    if (filename.empty())
    {
        filename = get_realmdir(realmname, true) + "/" + "sats.dat";
        fp = fopen(filename.c_str(), "r");
        if (fp == nullptr)
        {
            return -1;
        }
    }
    else
    {
        fp = fopen(filename.c_str(), "r");
        if (fp == nullptr)
        {
            filename = get_realmdir(realmname, true) + "/" + filename;
            fp = fopen(filename.c_str(), "r");
            if (fp == nullptr)
            {
                return -1;
            }
        }
    }

    if (fp != nullptr)
    {
        line.resize(1010);
        while (fgets((char *)line.data(), 1000, fp) != nullptr)
        {
            if (line.front() == '#') { continue; }
            iretn = ParseSatString(line);
            if (iretn < 0)
            {
                fclose(fp);
                return iretn;
            }
        }
        fclose(fp);
    }
    return cnodes.size();
}

//! \brief Add single satellite from line of JSON in a string.
//! \param args JSON line of satellite arguments.
//! \return Number of arguments, or negative error.

int32_t Simulator::ParseSatString(string args)
{
    int32_t iretn;
    double eventtick = 60.;
    double maxthrust = 0.;
    double maxtorque = 0.;
    double maxomega = 0.;
    double maxmoment = 0.;
    uint16_t argcount = 0;
    string nodename;
    string type;
    uint16_t nodetype = NODE_TYPE_SATELLITE;
    vector<camstruc> dets;
    locstruc satloc;
    string estring;
    json11::Json jargs = json11::Json::parse(args, estring);
    // The structural type lives in the node file (<realm>/nodes/<name>.json
    // "stype"); a leftover sats.dat "type" is ignored, loudly, rather than
    // letting the node quietly fall back to the default body.
    if (!jargs["type"].is_null())
    {
        std::cerr << "Warning: sats.dat \"type\" is no longer read (node \"" << jargs["nodename"].string_value()
                  << "\"): set \"stype\" in <realm>/nodes/<name>.json, e.g. with agent/scripts/migrate_sats.py." << std::endl;
    }
    if (!jargs["nodename"].is_null())
    {
        ++argcount;
        nodename = jargs["nodename"].string_value();
    }
    if (!jargs["nodetype"].is_null())
    {
        ++argcount;
        nodetype = jargs["nodetype"].long_value();
    }
    if (!jargs["detector"].is_null())
    {
        ++argcount;
        json11::Json::object values = jargs["detector"].object_items();
        camstruc det;
        det.fov = values["fov"].number_value();
        det.ifov = values["ifov"].number_value();
        det.specmin = values["specmin"].number_value();
        det.specmax = values["specmax"].number_value();
        det.name = "det_" + to_unsigned(dets.size());
        dets.push_back(det);
        AddDetector(det);
    }
    if (!jargs["eventtick"].is_null())
    {
        ++argcount;
        eventtick = jargs["eventtick"].number_value();
    }
    if (!jargs["maxthrust"].is_null())
    {
        ++argcount;
        maxthrust = jargs["maxthrust"].number_value();
    }
    if (!jargs["maxtorque"].is_null())
    {
        ++argcount;
        maxtorque = jargs["maxtorque"].number_value();
    }
    if (!jargs["maxomega"].is_null())
    {
        ++argcount;
        maxomega = jargs["maxomega"].number_value();
    }
    if (!jargs["maxmoment"].is_null())
    {
        ++argcount;
        maxmoment = jargs["maxmoment"].number_value();
    }
    if (!jargs["lvlh"].is_null())
    {
        ++argcount;
        json11::Json::object values = jargs["lvlh"].object_items();
        if (fastcalc)
        {
            satloc = cnodes[0]->currentinfo.node.loc;
        }
        else
        {
            satloc = initialloc;
        }
        satloc.pos.lvlh.s.col[0] = values["x"].number_value();
        satloc.pos.lvlh.s.col[1] = values["y"].number_value();
        satloc.pos.lvlh.s.col[2] = values["z"].number_value();
        satloc.pos.lvlh.v.col[0] = values["vx"].number_value();
        satloc.pos.lvlh.v.col[1] = values["vy"].number_value();
        satloc.pos.lvlh.v.col[2] = values["vz"].number_value();
        satloc.pos.lvlh.pass++;
        pos_origin2lvlh(satloc);
        eci2tle2(satloc.pos.eci, satloc.tle);
    }
    if (!jargs["ric"].is_null())
    {
        ++argcount;
        json11::Json::object values = jargs["ric"].object_items();
        if (fastcalc)
        {
            satloc = cnodes[0]->currentinfo.node.loc;
        }
        else
        {
            satloc = initialloc;
        }
        satloc.pos.lvlh.s.col[0] = values["r"].number_value();
        satloc.pos.lvlh.s.col[1] = values["i"].number_value();
        satloc.pos.lvlh.s.col[2] = values["c"].number_value();
        satloc.pos.lvlh.v.col[0] = values["vr"].number_value();
        satloc.pos.lvlh.v.col[1] = values["vi"].number_value();
        satloc.pos.lvlh.v.col[2] = values["vc"].number_value();
        ric2lvlh(length_rv(satloc.pos.eci.s), satloc.pos.lvlh, satloc.pos.lvlh);
        satloc.pos.lvlh.pass++;
        pos_origin2lvlh(satloc);
        eci2tle2(satloc.pos.eci, satloc.tle);
    }
    if (!jargs["eci"].is_null())
    {
        json11::Json::object values = jargs["eci"].object_items();
        satloc.pos.eci.utc = (values["utc"].number_value());
        satloc.pos.eci.s.col[0] = (values["x"].number_value());
        satloc.pos.eci.s.col[1] = (values["y"].number_value());
        satloc.pos.eci.s.col[2] = (values["z"].number_value());
        satloc.pos.eci.v.col[0] = (values["vx"].number_value());
        satloc.pos.eci.v.col[1] = (values["vy"].number_value());
        satloc.pos.eci.v.col[2] = (values["vz"].number_value());
        satloc.pos.eci.a.col[0] = (values["ax"].number_value());
        satloc.pos.eci.a.col[1] = (values["ay"].number_value());
        satloc.pos.eci.a.col[2] = (values["az"].number_value());
        pos_set_eci(satloc);
    }
    if (!cnodes.size())
    {
        if (nodename.empty())
        {
            nodename = "mother";
        }
        if (type.empty())
        {
            type = "HEX65W80H";
        }
        if (fastcalc && !flo_enabled)
        {
            iretn = AddNode(nodename, type, Physics::Propagator::PositionTle, Physics::Propagator::AttitudeIterative, Physics::Propagator::Thermal, Physics::Propagator::Electrical, initialloc.tle, initialloc.att.icrf);
        }
        else
        {
            // Use PositionLunar when a frozen lunar orbit was configured;
            // fall back to GaussJackson for Earth orbits.
            Physics::Propagator::Type ptype = flo_enabled
                ? flo_ptype
                : Physics::Propagator::PositionGaussJackson;
            iretn = AddNode(nodename, type, ptype, Physics::Propagator::AttitudeIterative, Physics::Propagator::Thermal, Physics::Propagator::Electrical, initialloc.pos.eci, initialloc.att.icrf);
        }
    }
    else
    {
        if (nodename.empty())
        {
            nodename = "child_" + to_unsigned(cnodes.size(), 2, true);
        }
        if (type.empty())
        {
            type = "U12XY";
        }
        if (fastcalc && !flo_enabled)
        {
            iretn = AddNode(nodename, type, Physics::Propagator::PositionTle, Physics::Propagator::AttitudeIterative, Physics::Propagator::Thermal, Physics::Propagator::Electrical, satloc.tle, initialloc.att.icrf);
        }
        else
        {
            Physics::Propagator::Type ptype = flo_enabled
                ? flo_ptype
                : Physics::Propagator::PositionGaussJackson;
            iretn = AddNode(nodename, type, ptype, Physics::Propagator::AttitudeIterative, Physics::Propagator::Thermal, Physics::Propagator::Electrical, satloc.pos.eci, initialloc.att.icrf);
        }
    }

    Physics::Simulator::StateList::iterator sit = GetNode(nodename);
    if (cnodes.size())
    {
        (*sit)->currentinfo.node.loc.pos.lvlh = satloc.pos.lvlh;
    }
    (*sit)->currentinfo.event_tick = eventtick / 86400.;
    (*sit)->currentinfo.node.type = nodetype;

    // // TSEN
    // for (uint16_t i=0; i<(*sit)->currentinfo.pieces.size(); ++i)
    // {
    //     iretn = json_createpiece(&(*sit)->currentinfo, "tsen_piece_" + (*sit)->currentinfo.pieces[i].name, DeviceType::TSEN);
    // }

    // // CPU
    // iretn = json_createpiece(&(*sit)->currentinfo, "obc_cpu", DeviceType::CPU);

    // // Disk
    // iretn = json_createpiece(&(*sit)->currentinfo, "obc_disk", DeviceType::DISK);

    // Thruster
        // json_createpiece(&(*sit)->currentinfo, "adcs_thrust", DeviceType::THST);
    for (uint16_t i=0; i<(*sit)->currentinfo.devspec.thst.size(); ++i)
    {
        (*sit)->currentinfo.devspec.thst[i].maxthrust = maxthrust;
    }
    // (*sit)->currentinfo.devspec.thst[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unitx(), rv_unitz(-1.), rv_unitx());
    // (*sit)->currentinfo.node.phys.maxthrust = maxthrust;

    // Reaction wheels
    for (uint16_t i=0; i<(*sit)->currentinfo.devspec.rw.size(); ++i)
    {
        (*sit)->currentinfo.devspec.rw[i].mxtrq = maxtorque;
        (*sit)->currentinfo.devspec.rw[i].mxomg = maxomega;
    }
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_rw_x", DeviceType::RW);
    // (*sit)->currentinfo.devspec.rw[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].mxtrq = maxtorque;
    // (*sit)->currentinfo.devspec.rw[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].mxomg = maxomega;
    // (*sit)->currentinfo.devspec.rw[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unity(), rv_unitx(), rv_unity());
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_rw_y", DeviceType::RW);
    // (*sit)->currentinfo.devspec.rw[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].mxtrq = maxtorque;
    // (*sit)->currentinfo.devspec.rw[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].mxomg = maxomega;
    // (*sit)->currentinfo.devspec.rw[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unitx(), rv_unity(), rv_unitx());
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_rw_z", DeviceType::RW);
    // (*sit)->currentinfo.devspec.rw[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].mxtrq = maxtorque;
    // (*sit)->currentinfo.devspec.rw[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].mxomg = maxomega;
    // (*sit)->currentinfo.devspec.rw[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unity(), rv_unitz(), rv_unity());
    (*sit)->currentinfo.node.phys.maxtorque = maxtorque;

    // Torque rods
    for (uint16_t i=0; i<(*sit)->currentinfo.devspec.mtr.size(); ++i)
    {
        (*sit)->currentinfo.devspec.mtr[i].mxmom = maxmoment;
    }
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_mtr_x", DeviceType::MTR);
    // (*sit)->currentinfo.devspec.mtr[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].mxmom = maxmoment;
    // (*sit)->currentinfo.devspec.mtr[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unity(), rv_unitx(), rv_unity());
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_mtr_y", DeviceType::MTR);
    // (*sit)->currentinfo.devspec.mtr[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].mxmom = maxmoment;
    // (*sit)->currentinfo.devspec.mtr[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unitx(), rv_unity(), rv_unitx());
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_mtr_z", DeviceType::MTR);
    // (*sit)->currentinfo.devspec.mtr[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].mxmom = maxmoment;
    // (*sit)->currentinfo.devspec.mtr[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unity(), rv_unitz(), rv_unity());

    // // GPS
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_gps", DeviceType::GPS);

    // // Magnetometer
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_mag", DeviceType::MAG);
    // (*sit)->currentinfo.devspec.mag[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unity(), rv_unitx(), rv_unity());

    // // Gyros
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_gyro_x", DeviceType::GYRO);
    // (*sit)->currentinfo.devspec.gyro[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unity(), rv_unitx(), rv_unity());
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_gyro_y", DeviceType::GYRO);
    // (*sit)->currentinfo.devspec.gyro[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unitx(), rv_unity(), rv_unitx());
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_gyro_z", DeviceType::GYRO);
    // (*sit)->currentinfo.devspec.gyro[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unity(), rv_unitz(), rv_unity());

    // // Star trackers
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_stt_x+", DeviceType::STT);
    // (*sit)->currentinfo.devspec.stt[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unity(), rv_unitx(), rv_unity());
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_stt_x-", DeviceType::STT);
    // (*sit)->currentinfo.devspec.stt[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unity(), rv_unitx(-1.0), rv_unity());

    // // Sun and Earth sensors
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_xyzsen_sun", DeviceType::XYZSEN);
    // //        (*sit)->currentinfo.devspec.xyzsen[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unity(), rv_unitz(-1.0), rv_unity());
    // iretn = json_createpiece(&(*sit)->currentinfo, "adcs_xyzsen_earth", DeviceType::XYZSEN);
    // //        (*sit)->currentinfo.devspec.xyzsen[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].align = q_irotate_for(rv_unitz(), rv_unity(), rv_unitz(), rv_unity());


    // Detectors
    for (uint16_t i=0; i<4; ++i)
    {
        if (i == dets.size())
        {
            break;
        }
        if (i == (*sit)->currentinfo.devspec.cam.size())
        {
            std::cerr << "Not enough cam devices (" << std::to_string((*sit)->currentinfo.devspec.cam.size()) << ") for number of detectors (" << std::to_string(dets.size()) << ")!" << std::endl;
            break;
        }
        (*sit)->currentinfo.devspec.cam[i].volt = 5.;
        (*sit)->currentinfo.devspec.cam[i].amp = 20. / dets[i].volt;
        (*sit)->currentinfo.devspec.cam[i].state = 0;
        (*sit)->currentinfo.devspec.cam[i].fov = dets[i].fov;
        (*sit)->currentinfo.devspec.cam[i].ifov = dets[i].ifov;
        (*sit)->currentinfo.devspec.cam[i].specmin = dets[i].specmin;
        (*sit)->currentinfo.devspec.cam[i].specmax = dets[i].specmax;
    }
    // for (camstruc det : dets)
    // {
    //     iretn = json_createpiece(&(*sit)->currentinfo, det.name, DeviceType::CAM);
    //     (*sit)->currentinfo.devspec.cam[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].volt = 5.;
    //     (*sit)->currentinfo.devspec.cam[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].amp = 20. / det.volt;
    //     (*sit)->currentinfo.devspec.cam[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].state = 0;
    //     (*sit)->currentinfo.devspec.cam[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].fov = det.fov;
    //     (*sit)->currentinfo.devspec.cam[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].ifov = det.ifov;
    //     (*sit)->currentinfo.devspec.cam[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].specmin = det.specmin;
    //     (*sit)->currentinfo.devspec.cam[(*sit)->currentinfo.device[(*sit)->currentinfo.pieces[iretn].cidx]->didx].specmax = det.specmax;
    // }

    // json_map_node(&(*sit)->currentinfo);
    //    jsonnode json;
    //    json_setup_node(json, &(*sit)->currentinfo, false);
    // json_updatecosmosstruc(&(*sit)->currentinfo);

    (*sit)->sohstring = json_list_of_soh(&(*sit)->currentinfo);

    return argcount;
}

//! \brief Add targets from a targets file: a JSON array of target shortcuts
//! (see json_target_payload()), normally <realm>/targets.json.
//! \param filename Path to the file, or a name relative to the realm directory.
//! Empty means <realm>/targets.json.
//! \return Number of targets, or negative error.

int32_t Simulator::ParseTargetFile(string filename)
{
    string realmdir = get_realmdir(realmname, true);
    if (filename.empty())
    {
        filename = realmdir + "/targets.json";
    }
    else if (access(filename.c_str(), R_OK) != 0)
    {
        filename = realmdir + "/" + filename;
    }

    vector<targetstruc> loaded;
    int32_t iretn = json_targets_load(filename, loaded);
    if (iretn < 0)
    {
        return iretn;
    }
    for (targetstruc &targ : loaded)
    {
        AddTarget(targ);
    }
    return targets.size();
}

//! \brief Add targets from a JSON object keyed by target name, each value a
//! target shortcut (see json_target_payload()). The key supplies the name.
//! \param jargs Parsed JSON object.
//! \return Zero, or negative error if any entry is malformed.

int32_t Simulator::ParseTargetJson(json11::Json jargs)
{
    int32_t iretn = 0;
    for (const auto& item : jargs.object_items())
    {
        targetstruc targ;
        targ.name = item.first;
        int32_t itemret = json_target_payload_load(targ, item.second);
        if (itemret < 0)
        {
            iretn = itemret;
            continue;
        }
        AddTarget(targ);
    }
    return iretn;
}

//! \brief Add every ground station node in the realm as a target.
//!
//! Ground stations are real nodes (NODE_TYPE_GROUNDSTATION files under
//! <realm>/nodes/), not rows in targets.json. Each is synthesized into a
//! target so contact/pointing math treats it like any other. MOCs are logical
//! nodes and are never added. Ground stations are fixed points, so this runs
//! once, after the propagated nodes exist.
//! \return Number of ground stations added, or negative error.

int32_t Simulator::AddGroundStations()
{
    vector<nodestruc> stations;
    int32_t iretn = json_load_realm_groundstations(realmname, stations);
    if (iretn < 0)
    {
        return iretn;
    }
    for (nodestruc &station : stations)
    {
        targetstruc targ = target_from_node(station);
        AddTarget(targ);
    }
    return stations.size();
}

//! \brief Nudges Node in the desired direction
//!
//!
int32_t Simulator::NudgeNode(string nodename, cartpos pos, qatt att)
{
    auto node = GetNode(nodename);
    if (node == cnodes.end())
    {
        return 0;
    }

    Vector newpush;
    if (pos.utc != 0.)
    {
        switch((*node)->ptype)
        {
        case Propagator::Type::PositionIterative:
            (*node)->currentinfo.node.loc.pos.eci.s += pos.s;
            (*node)->currentinfo.node.loc.pos.eci.v += pos.v;
            (*node)->currentinfo.node.loc.pos.eci.a += pos.a;
            (*node)->currentinfo.node.loc.pos.eci.j += pos.j;
            pos_set_eci((*node)->currentinfo.node.loc);
            (*node)->currentinfo.node.phys.fpush = rv_zero();
            break;
        case Propagator::Type::PositionInertial:
            (*node)->currentinfo.node.loc.pos.icrf.s += pos.s;
            (*node)->currentinfo.node.loc.pos.icrf.v += pos.v;
            (*node)->currentinfo.node.loc.pos.icrf.a += pos.a;
            (*node)->currentinfo.node.loc.pos.icrf.j += pos.j;
            pos_icrf((*node)->currentinfo.node.loc);
            (*node)->currentinfo.node.phys.fpush = rv_zero();
            break;
        case Propagator::Type::PositionGaussJackson:
        {
            double dt = 86400.* (pos.utc - (*node)->currentinfo.node.loc.pos.eci.utc);
            double dt2 = dt * dt;
            double dt3 = dt2 * dt;
            Vector s1 = Vector((*node)->currentinfo.node.loc.pos.eci.s);
            Vector v1 = Vector((*node)->currentinfo.node.loc.pos.eci.v);
            Vector a1 = Vector((*node)->currentinfo.node.loc.pos.eci.a);
            Vector j1 = Vector((*node)->currentinfo.node.loc.pos.eci.j);
            Vector s2 = s1 + v1 * dt + a1 * dt2 / 2. + j1 * dt3 / 6.;
            Vector v2 = v1 + a1 * dt + j1 * dt2 / 2.;
            Vector a2 = a1 + j1 * dt;
            Vector sp = Vector(pos.s);
            Vector vp = Vector(pos.v);
            Vector ap = Vector(pos.a);
            Vector ds1 = sp - s1;
            double deltas = ds1.norm();
            Vector ds2 = sp - s2;
            Vector as = 2. * ds2 / dt2;
            Vector dv1 = vp - v1;
            double deltav = dt * dv1.norm();
            Vector dv2 = vp - v2;
            Vector av = dv2 / dt;
            Vector da1 = ap - a1;
            double deltaa = dt2 * da1.norm();
            Vector da2 = ap - a2;
            Vector aa = da2;
            double delta = deltas + deltav + deltaa;
            Vector acc = (delta * aa + deltav * av + deltas * as) / delta;
            (*node)->currentinfo.node.phys.fpush = acc.to_rv();
        }
        break;
        case Propagator::Type::PositionGeo:
            (*node)->currentinfo.node.loc.pos.geod.s.lon += pos.s.col[0];
            (*node)->currentinfo.node.loc.pos.geod.s.lat += pos.s.col[1];
            (*node)->currentinfo.node.loc.pos.geod.s.h += pos.s.col[2];
            (*node)->currentinfo.node.loc.pos.geod.v.lon += pos.v.col[0];
            (*node)->currentinfo.node.loc.pos.geod.v.lat += pos.v.col[1];
            (*node)->currentinfo.node.loc.pos.geod.v.h += pos.v.col[2];
            (*node)->currentinfo.node.loc.pos.geod.a.lon += pos.a.col[0];
            (*node)->currentinfo.node.loc.pos.geod.a.lat += pos.a.col[1];
            (*node)->currentinfo.node.loc.pos.geod.a.h += pos.a.col[2];
            pos_set_geod((*node)->currentinfo.node.loc);
            (*node)->currentinfo.node.phys.fpush = rv_zero();
            break;
        case Propagator::Type::PositionTle:
            tle2eci((*node)->currentinfo.node.loc.pos.eci.utc, (*node)->currentinfo.node.loc.tle, (*node)->currentinfo.node.loc.pos.eci);
            (*node)->currentinfo.node.loc.pos.eci.s += pos.s;
            (*node)->currentinfo.node.loc.pos.eci.v += pos.v;
            (*node)->currentinfo.node.loc.pos.eci.a += pos.a;
            (*node)->currentinfo.node.loc.pos.eci.j += pos.j;
            pos_set_eci((*node)->currentinfo.node.loc);
            eci2tle2((*node)->currentinfo.node.loc.pos.eci, (*node)->currentinfo.node.loc.tle);
            (*node)->currentinfo.node.phys.fpush = rv_zero();
            break;
        case Propagator::Type::PositionLvlh:
        {
            pos_lvlh2origin((*node)->currentinfo.node.loc);
            (*node)->currentinfo.node.loc.pos.lvlh.s += pos.s;
            (*node)->currentinfo.node.loc.pos.lvlh.v += pos.v;
            (*node)->currentinfo.node.loc.pos.lvlh.a += pos.a;
            (*node)->currentinfo.node.loc.pos.lvlh.j += pos.j;
            (*node)->currentinfo.node.loc.pos.lvlh.pass++;
            pos_origin2lvlh((*node)->currentinfo.node.loc);
            double dt = 86400.* (pos.utc - (*node)->currentinfo.node.loc.pos.eci.utc);
            double dt2 = dt * dt;
            double dt3 = dt2 * dt;
            Vector s1 = Vector((*node)->currentinfo.node.loc.pos.eci.s);
            Vector v1 = Vector((*node)->currentinfo.node.loc.pos.eci.v);
            Vector a1 = Vector((*node)->currentinfo.node.loc.pos.eci.a);
            Vector j1 = Vector((*node)->currentinfo.node.loc.pos.eci.j);
            Vector s2 = s1 + v1 * dt + a1 * dt2 / 2. + j1 * dt3 / 6.;
            Vector v2 = v1 + a1 * dt + j1 * dt2 / 2.;
            Vector a2 = a1 + j1 * dt;
            Vector sp = Vector(pos.s);
            Vector vp = Vector(pos.v);
            Vector ap = Vector(pos.a);
            Vector ds1 = sp - s1;
            double deltas = ds1.norm();
            Vector ds2 = sp - s2;
            Vector as = 2. * ds2 / dt2;
            Vector dv1 = vp - v1;
            double deltav = dt * dv1.norm();
            Vector dv2 = vp - v2;
            Vector av = dv2 / dt;
            Vector da1 = ap - a1;
            double deltaa = dt2 * da1.norm();
            Vector da2 = ap - a2;
            Vector aa = da2;
            double delta = deltas + deltav + deltaa;
            Vector acc = (delta * aa + deltav * av + deltas * as) / delta;
            (*node)->currentinfo.node.phys.fpush = acc.to_rv();
        }
        break;
        default:
            break;
        }
    }
    if (att.utc != 0.)
    {

    }

    Vector newtorque;
    UpdateTorque(nodename, newtorque);
    return 1;
}

/**
         * \brief Addes a node to be propagated, in descending order of priority
         * 
         * \param nodename Name of node
         * \param propagation_priority Lower values are propagated before higher values
         * \return int32_t 
         */
Simulator::StateList::iterator Simulator::AddNode(string nodename, uint8_t propagation_priority)
{
    Physics::State* newstate = new Physics::State(nodename, propagation_priority);

    // <realm>/nodes/<name>.json, if present, names the body (stype) and adds or
    // adjusts hardware over the generated baseline; see State::Init.
    ifstream nodefile(get_realmdir(realmname) + "/nodes/" + nodename + ".json");
    if (nodefile.is_open())
    {
        newstate->overlay.assign((std::istreambuf_iterator<char>(nodefile)), std::istreambuf_iterator<char>());
    }

    StateList::iterator it = std::upper_bound(cnodes.begin(), cnodes.end(), newstate, compareByPriority);
    return cnodes.insert(it, newstate);
}

int32_t Simulator::AddNode(string nodename, string stype, Propagator::Type ptype, Propagator::Type atype, Propagator::Type ttype, Propagator::Type etype, Convert::tlestruc tle, Convert::qatt icrf, uint8_t propagation_priority)
{
    auto it = AddNode(nodename, propagation_priority);
    error = (*it)->Init(nodename, dt, stype, ptype, atype, ttype, etype, tle, currentutc, icrf);
    if (error < 0)
    {
        return error;
    }
    return cnodes.size();
}

int32_t Simulator::AddNode(string nodename, string stype, Propagator::Type ptype, Propagator::Type atype, Propagator::Type ttype, Propagator::Type etype, Convert::cartpos eci, Convert::qatt icrf, uint8_t propagation_priority)
{
    auto it = AddNode(nodename, propagation_priority);
    error = (*it)->Init(nodename, dt, stype, ptype, atype, ttype, etype, eci, icrf);
    if (error < 0)
    {
        return error;
    }
    error = (*it)->Propagate(currentutc);
    if (error < 0)
    {
        return error;
    }
    return cnodes.size();
}

/**
         * \brief Add a node with an LVLH position propagator
         * 
         * \param nodename Name of node
         * \param stype Structural type
         * \param ptype Position propagator
         * \param atype Attitude propagator
         * \param ttype Thermal propagator
         * \param etype Electrical propagator
         * \param oeventtype Orbital Events propagator
         * \param lvlh LVLH coordinates
         * \param origineci The origin of the LVLH frame in geocentric frame
         * \param icrf Attitude of node
         * \return int32_t 0 on success, negative on error
         */
int32_t Simulator::AddNode(string nodename, string stype, Propagator::Type ptype, Propagator::Type atype, Propagator::Type ttype, Propagator::Type etype, Convert::cartpos origineci, Convert::cartpos lvlh, Convert::qatt icrf, uint8_t propagation_priority)
{
    auto it = AddNode(nodename, propagation_priority);
    error = (*it)->Init(nodename, dt, stype, ptype, atype, ttype, etype, origineci, lvlh, icrf);
    if (error < 0)
    {
        return error;
    }
    error = (*it)->Propagate(currentutc);
    if (error < 0)
    {
        return error;
    }
    return cnodes.size();
}

int32_t Simulator::AddNode(string nodename, string stype, Propagator::Type ptype, Propagator::Type atype, Propagator::Type ttype, Propagator::Type etype, double utc, double latitude, double longitude, double altitude, double angle, double timeshift)
{
    auto it = AddNode(nodename, 0);
    Convert::locstruc loc;
    if (ptype == Propagator::PositionGeo)
    {
        loc.pos.geod.s.lat = latitude;
        loc.pos.geod.s.lon = longitude;
        loc.pos.geod.s.h = altitude;
        loc.pos.geod.v = gv_zero();
        loc.pos.geod.a = gv_zero();
        loc.pos.geod.utc = utc;
        Convert::pos_set_geod(loc);
        loc.att.geoc.s = q_eye();
        loc.att.geoc.v = rv_zero();
        loc.att.geoc.a = rv_zero();
        loc.att.geoc.utc = utc;
        Convert::att_set_geoc(loc);
    }
    else
    {
        loc = Physics::shape2eci(utc, latitude, longitude, altitude, angle, timeshift);
        loc.att.lvlh.s = q_eye();
        loc.att.lvlh.v = rv_zero();
        loc.att.lvlh.a = rv_zero();
        loc.att.lvlh.utc = utc;
        Convert::att_set_lvlh(loc);
    }
    error = (*it)->Init(nodename, dt, stype, ptype, atype, ttype, etype, loc.pos.eci, loc.att.icrf);
    if (error < 0)
    {
        return error;
    }
    error = (*it)->Propagate(currentutc);
    if (error < 0)
    {
        return error;
    }
    return cnodes.size();
}

int32_t Simulator::GetError()
{
    return error;
}

int32_t Simulator::Reset()
{
    RunState = State::Paused;
    currentutc = initialutc;
    // Queues are reloaded from their files on the next step
    queues.clear();
    attcommands.clear();
    imaging.clear();
    for (auto &state : cnodes)
    {
        state->Reset(currentutc);
    }
    return error;
}

int32_t Simulator::Propagate(double nextutc, bool enablethrust, bool enabletorque)
{
    int iretn = 0;
    if (nextutc == 0.)
    {
        currentutc += dtj;
    }
    else {
        currentutc = nextutc;
    }

    // Formation geometry + thrust: set loc_req.pos for children, then compute fpush.
    if (enablethrust && cnodes.size() > 1 && !m_formationType.empty())
    {
        Formation(m_formationType, m_formationSpacing);
        Thrust();
    }

    for (auto &state : cnodes)
    {
        switch(state->ptype)
        {
        case Physics::Propagator::Type::PositionInertial:
        case Physics::Propagator::Type::PositionIterative:
        case Physics::Propagator::Type::PositionGaussJackson:
        case Physics::Propagator::Type::PositionGeo:
        case Physics::Propagator::Type::PositionTle:
            iretn = state->Propagate(currentutc);
            break;
        case Physics::Propagator::Type::PositionLunar:
            iretn = state->Propagate(currentutc);
            break;
        case Physics::Propagator::Type::PositionLvlh:
            iretn = state->Propagate(cnodes[0]->currentinfo.node.loc);
            break;
        default:
            break;
        }
    }

    // Apply Sun-synchronous nodal precession.
    // Rotates every node's ECI position and velocity about the Earth's spin
    // axis (Z) by the amount the orbital plane must drift this step so that
    // it tracks the Sun.  This gives an exact analytical precession rate
    // (SSO_SUN_RATE) regardless of whether the integrator models J2.
    // Only active when ParseOrbitString processed an "sso" block.
    if (sso_enabled)
    {
        double delta_raan = sso_raan_rate * dt;   // rad/step (dt is in seconds)
        double c = cos(delta_raan);
        double s = sin(delta_raan);
        for (auto &state : cnodes)
        {
            cartpos &eci = state->currentinfo.node.loc.pos.eci;
            double x, y;
            // Rotate position
            x = eci.s.col[0];  y = eci.s.col[1];
            eci.s.col[0] = c*x - s*y;
            eci.s.col[1] = s*x + c*y;
            // Rotate velocity
            x = eci.v.col[0];  y = eci.v.col[1];
            eci.v.col[0] = c*x - s*y;
            eci.v.col[1] = s*x + c*y;
            // Rotate acceleration
            x = eci.a.col[0];  y = eci.a.col[1];
            eci.a.col[0] = c*x - s*y;
            eci.a.col[1] = s*x + c*y;
            pos_set_eci(state->currentinfo.node.loc);
        }
    }

    // Apply Frozen Lunar Orbit nodal precession.
    // Rotates every node's ECI position and velocity about the Moon's spin
    // axis (Z in the Moon-centred inertial frame, approximated as ECI-Z for
    // short simulations) by the J2 secular RAAN drift for this timestep.
    // This corrects the fixed-plane ECI spiral into the slowly precessing
    // plane that characterises a true frozen lunar orbit.
    // Only active when ParseOrbitString processed a "flo" block.
    if (flo_enabled)
    {
        double delta_raan = flo_raan_rate_rads * dt;  // rad/step (dt in seconds)
        double c = cos(delta_raan);
        double s = sin(delta_raan);
        for (auto &state : cnodes)
        {
            cartpos &eci = state->currentinfo.node.loc.pos.eci;
            double x, y;
            // Rotate position
            x = eci.s.col[0];  y = eci.s.col[1];
            eci.s.col[0] = c*x - s*y;
            eci.s.col[1] = s*x + c*y;
            // Rotate velocity
            x = eci.v.col[0];  y = eci.v.col[1];
            eci.v.col[0] = c*x - s*y;
            eci.v.col[1] = s*x + c*y;
            // Rotate acceleration
            x = eci.a.col[0];  y = eci.a.col[1];
            eci.a.col[0] = c*x - s*y;
            eci.a.col[1] = s*x + c*y;
            ++eci.pass;
        }
    }

    // Use a copy of the mother's locstruc so that pos_planec2lvlh's internal
    // pos_set_geoc()/pos_set_selc() call (which wipes and recomputes the first
    // argument) does not corrupt the GJ integrator's primary ECI state variable.
    // LVLH is about the body each node is closest to (geoc, or selc at the Moon).
    locstruc mother_loc = cnodes[0]->currentinfo.node.loc;
    for (uint16_t i=1; i<cnodes.size(); ++i)
    {
        pos_planec2lvlh(mother_loc, cnodes[i]->currentinfo.node.loc);
        cnodes[i]->currentinfo.node.loc.pos.lvlh.s = -cnodes[i]->currentinfo.node.loc.pos.lvlh.s;
        cnodes[i]->currentinfo.node.loc.pos.lvlh.v = -cnodes[i]->currentinfo.node.loc.pos.lvlh.v;
        cnodes[i]->currentinfo.node.loc.pos.lvlh.a = -cnodes[i]->currentinfo.node.loc.pos.lvlh.a;
    }

    // Command queues: run due Events against the propagated state
    ParseCommands();

    // Attitude control: slerp each node toward its desired pointing direction.
    // Runs after position propagation so update_target() results are current.
    if (enabletorque)
        AttitudeStep();

    return iretn;
}

//! Load a node's command queue
//! By default the queue is the standing set of command events in the node file,
//! the "command_events" array of <realm>/nodes/<node>.json: each element is an Event
//! (name, type, flag, data, condition, utc, timeout; other keys are ignored). The node
//! starts as if it had just booted with that set. A real agent's runtime state
//! (temp/exec/.queue) and uplinked .command files are not read. A missing file or
//! array gives an empty queue.
//! \param nodename Node name.
//! \param filepath A saved queue file (CommandQueue::save_commands() format) to load
//! instead, e.g. for tests, or empty for the node file.
//! \return Number of commands in the queue, or negative error.
int32_t Simulator::LoadQueue(string nodename, string filepath)
{
    auto &queue = queues[nodename];
    queue.reset(new Support::CommandQueue);
    if (filepath.empty())
    {
        ifstream nodefile(get_realmdir(realmname) + "/nodes/" + nodename + ".json");
        if (!nodefile.is_open())
        {
            return 0;
        }
        string contents((std::istreambuf_iterator<char>(nodefile)), std::istreambuf_iterator<char>());
        string error;
        json11::Json jnode = json11::Json::parse(contents, error);
        if (!error.empty())
        {
            return JSON_ERROR_SCAN;
        }
        for (const json11::Json &jevent : jnode["command_events"].array_items())
        {
            Event cmd;
            cmd.set_command(jevent.dump());
            if (cmd.is_command())
            {
                queue->add_command(cmd);
            }
        }
        return queue->get_command_size();
    }
    size_t slash = filepath.find_last_of('/');
    if (slash == string::npos)
    {
        return queue->restore_commands("", filepath);
    }
    return queue->restore_commands(filepath.substr(0, slash+1), filepath.substr(slash+1));
}

//! Run each node's due commands at the current simulated time
//! Recognized requests are interpreted in-process: request_adcs point sets the
//! requested attitude (loc_req.att), request_eps switches a device on or off, and
//! request_camera snap starts an imaging session (one per request, so sessions on
//! different cameras run independently). Every due Event is added to the node's event list.
//! \return Number of Events run, or negative error.
int32_t Simulator::ParseCommands()
{
    int32_t count = 0;
    for (auto &state : cnodes)
    {
        string name = state->currentinfo.node.name;
        if (queues.find(name) == queues.end())
        {
            LoadQueue(name);
        }
        // Sessions already running take their frames first
        ImagingStep(*state);
        auto &queue = queues[name];
        if (!queue->get_command_size())
        {
            continue;
        }
        for (Event &cmd : queue->due_commands(&state->currentinfo, currentutc))
        {
            eventstruc cevent;
            cevent.name = cmd.name;
            cevent.type = cmd.type;
            cevent.data = cmd.data;
            cevent.condition = cmd.condition;
            cevent.utc = currentutc;
            cevent.utcexec = currentutc;
            cevent.flag = cmd.flag;
            if (cmd.type == EVENT_TYPE_REQUEST)
            {
                vector<string> args = string_split(cmd.data);
                if (!args.empty() && args[0] == "request_adcs")
                {
                    AttitudeCommand point;
                    rvector dir;
                    if (Support::Agent::parse_adcs_point(cmd.data, point) < 0)
                    {
                        cevent.flag = (cevent.flag & ~EVENT_FLAG_COLOR) | EVENT_FLAG_COLOR_RED;
                    }
                    else if (point.mode == "hold")
                    {
                        attcommands.erase(name);
                    }
                    // A named target or station must exist (nearest may have none in view yet)
                    else if (point.mode == "aim"
                             && ((point.aim.name.size() && point.aim.name != "nearest" && aimDirection(state->currentinfo, point.aim, dir) < 0)
                                 || (point.align.name.size() && point.align.name != "nearest" && aimDirection(state->currentinfo, point.align, dir) < 0)))
                    {
                        cevent.flag = (cevent.flag & ~EVENT_FLAG_COLOR) | EVENT_FLAG_COLOR_RED;
                    }
                    else
                    {
                        attcommands[name] = point;
                    }
                }
                else if (!args.empty() && args[0] == "request_eps")
                {
                    if (EpsSwitch(*state, cmd.data) < 0)
                    {
                        cevent.flag = (cevent.flag & ~EVENT_FLAG_COLOR) | EVENT_FLAG_COLOR_RED;
                    }
                }
                else if (!args.empty() && args[0] == "request_camera")
                {
                    string camera;
                    uint32_t frames, tile;
                    double rate, inttime;
                    devicestruc *dev = nullptr;
                    if (Support::Agent::parse_camera_snap(cmd.data, camera, frames, rate, inttime, tile) >= 0
                        && (camera.empty() || ((dev = findDevice(state->currentinfo, camera)) != nullptr
                                               && dev->type == static_cast<uint16_t>(DeviceType::CAM))))
                    {
                        // Reported by ImagingStep() when the session ends
                        ImagingSession session;
                        session.name = cmd.name;
                        session.request = cmd.data;
                        session.camera = camera;
                        session.flag = cmd.flag;
                        if (cmd.is_conditional())
                        {
                            session.condition.parse(cmd.condition);
                        }
                        session.frames = frames;
                        session.tile = tile;
                        session.period = 1. / (rate * 86400.);
                        session.inttime = inttime;
                        session.nextutc = currentutc;
                        imaging[name].push_back(session);
                        continue;
                    }
                    else
                    {
                        cevent.flag = (cevent.flag & ~EVENT_FLAG_COLOR) | EVENT_FLAG_COLOR_RED;
                    }
                }
            }
            state->currentinfo.event.push_back(cevent);
            ++count;
        }
        // New sessions take their first frame now
        ImagingStep(*state);
    }
    return count;
}

//! Find a device by its piece name
//! \param info Node to search.
//! \param name Piece name, as in the node file.
//! \return The device, or nullptr when there is no such piece or it has no device.
devicestruc *Simulator::findDevice(cosmosstruc &info, const string &name)
{
    int32_t pidx = json_findpiece(&info, name);
    if (pidx < 0)
    {
        return nullptr;
    }
    // A piece without a device has no cidx of its own
    uint16_t cidx = info.pieces[pidx].cidx;
    if (cidx >= info.device.size() || info.device[cidx]->pidx != pidx)
    {
        return nullptr;
    }
    return info.device[cidx];
}

//! Switch a device on or off for request_eps
//! On powers the device at its nominal draw, as State::Init() does for devices
//! flagged DEVICE_FLAG_ON; off zeroes its draw. The electrical propagator picks
//! up the change on its next step.
//! \param state Node to act on.
//! \param request request_eps on|off device
//! \return Zero or negative error.
int32_t Simulator::EpsSwitch(Physics::State &state, const string &request)
{
    bool on;
    string device;
    int32_t iretn = Support::Agent::parse_eps_switch(request, on, device);
    if (iretn < 0)
    {
        return iretn;
    }
    devicestruc *dev = findDevice(state.currentinfo, device);
    if (dev == nullptr)
    {
        return GENERAL_ERROR_NAME;
    }
    if (on)
    {
        dev->state = 1;
        dev->amp = dev->namp;
        dev->volt = dev->nvolt;
    }
    else
    {
        dev->state = 0;
        dev->amp = 0.;
        dev->volt = 0.;
    }
    dev->power = dev->amp * dev->volt;
    return 0;
}

//! Take the frames each of a node's imaging sessions is allowed up to the current time
//! Frames are due every period from the start of the session. A session ends when all
//! frames are taken (never, for frames = 0: continuous), or when its condition is false
//! (the condition is evaluated at the step, and is taken to hold for the whole step when
//! true). Frames are grouped into tiles of up to session.tile frames; each tile is one
//! file, reported by ImagingTile() when it fills or the session ends.
void Simulator::ImagingStep(Physics::State &state)
{
    auto sessions = imaging.find(state.currentinfo.node.name);
    if (sessions == imaging.end())
    {
        return;
    }
    for (auto session = sessions->second.begin(); session != sessions->second.end(); )
    {
        bool allowed = !session->condition.valid() || session->condition.evaluate(&state.currentinfo) != 0.;
        if (allowed)
        {
            while ((!session->frames || session->taken < session->frames) && session->nextutc <= currentutc)
            {
                if (!session->tiletaken)
                {
                    session->firstutc = session->nextutc;
                }
                session->lastutc = session->nextutc;
                ++session->tiletaken;
                ++session->taken;
                session->nextutc += session->period;
                // Tile full: save it and restart the take
                if (session->tiletaken == session->tile)
                {
                    ImagingTile(state, *session, false);
                }
            }
        }
        if (allowed && (!session->frames || session->taken < session->frames))
        {
            ++session;
            continue;
        }

        // Session over: save the partial tile, or report a session that took nothing
        bool shortened = session->frames && session->taken < session->frames;
        if (session->tiletaken || !session->taken)
        {
            ImagingTile(state, *session, shortened);
        }
        session = sessions->second.erase(session);
    }
}

//! Report the current tile of an imaging session as one event (one file) and start a new tile
//! utc is the tile's first frame, dtime runs to the end of its last frame's integration,
//! value is the number of frames, and data is the snap request, which names the camera
//! when one was given. Red if the session ended short of its frames.
void Simulator::ImagingTile(Physics::State &state, ImagingSession &session, bool shortened)
{
    eventstruc cevent;
    cevent.name = session.name;
    cevent.type = EVENT_TYPE_REQUEST;
    cevent.data = session.request;
    cevent.condition = session.condition.text();
    cevent.flag = session.flag;
    cevent.value = session.tiletaken;
    if (session.tiletaken)
    {
        cevent.utc = session.firstutc;
        cevent.dtime = 86400. * (session.lastutc - session.firstutc) + session.inttime;
    }
    else
    {
        cevent.utc = currentutc;
    }
    cevent.utcexec = cevent.utc;
    if (shortened)
    {
        cevent.flag = (cevent.flag & ~EVENT_FLAG_COLOR) | EVENT_FLAG_COLOR_RED;
    }
    state.currentinfo.event.push_back(cevent);
    session.tiletaken = 0;
}

int32_t Simulator::Propagate(vector<vector<cosmosstruc> > &results, uint32_t runcount)
{
    vector<cosmosstruc> result;
    result.resize(cnodes.size());
    for (uint16_t i=0; i<cnodes.size(); ++i)
    {
        update_metrics(&cnodes[i]->currentinfo);
        result[i].node = cnodes[i]->currentinfo.node;
        result[i].event = cnodes[i]->currentinfo.event;
        result[i].target = cnodes[i]->currentinfo.target;
        result[i].devspec = cnodes[i]->currentinfo.devspec;
    }
    results.push_back(result);
    uint32_t runtotal = 0;
    while (runtotal++ < runcount)
    {
        Propagate(0.);
        for (uint16_t i=0; i<cnodes.size(); ++i)
        {
            result[i].node = cnodes[i]->currentinfo.node;
            result[i].event = cnodes[i]->currentinfo.event;
            result[i].target = cnodes[i]->currentinfo.target;
            result[i].devspec = cnodes[i]->currentinfo.devspec;
        }
        results.push_back(result);
    }

    return results.size();
}

int32_t Simulator::Target(map<uint32_t, vector<pointing_info> > &pschedule)
{
    if (pschedule.size() > 1 && pschedule.begin()->second.size() >= cnodes.size())
    {
        if (currentutc < pschedule.begin()->second[0].pointing.utc)
        {
            for (uint16_t j=0; j<cnodes.size(); ++j)
            {
                cnodes[j]->currentinfo.target_idx = pschedule.begin()->second[j].target_idx;
                cnodes[j]->currentinfo.node.loc.att.lvlh = pschedule.begin()->second[j].pointing;
                cnodes[j]->currentinfo.node.loc.att.lvlh.pass = std::max(cnodes[j]->currentinfo.node.loc.att.icrf.pass, cnodes[j]->currentinfo.node.loc.att.geoc.pass) + 1;
                att_set_lvlh(cnodes[j]->currentinfo.node.loc);
            }
        }
        else if (currentutc > pschedule.rbegin()->second[0].pointing.utc)
        {
            for (uint16_t j=0; j<cnodes.size(); ++j)
            {
                cnodes[j]->currentinfo.target_idx = pschedule.rbegin()->second[j].target_idx;
                cnodes[j]->currentinfo.node.loc.att.lvlh = pschedule.rbegin()->second[j].pointing;
                cnodes[j]->currentinfo.node.loc.att.lvlh.pass = std::max(cnodes[j]->currentinfo.node.loc.att.icrf.pass, cnodes[j]->currentinfo.node.loc.att.geoc.pass) + 1;
                att_set_lvlh(cnodes[j]->currentinfo.node.loc);
            }
        }
        else
        {
            for (uint16_t j=0; j<cnodes.size(); ++j)
            {
                cnodes[j]->currentinfo.target_idx = pschedule[decisec(currentutc)][j].target_idx;
                cnodes[j]->currentinfo.node.loc.att.lvlh = pschedule[decisec(currentutc)][j].pointing;
                cnodes[j]->currentinfo.node.loc.att.lvlh.pass = std::max(cnodes[j]->currentinfo.node.loc.att.icrf.pass, cnodes[j]->currentinfo.node.loc.att.geoc.pass) + 1;
                att_set_lvlh(cnodes[j]->currentinfo.node.loc);
            }
        }
        return pschedule.size();
    }
    else
    {
        for (uint16_t j=0; j<cnodes.size(); ++j)
        {
            cnodes[j]->currentinfo.node.loc.att.lvlh.s = q_eye();
            cnodes[j]->currentinfo.node.loc.att.lvlh.v = rv_zero();
            cnodes[j]->currentinfo.node.loc.att.lvlh.a = rv_zero();
            cnodes[j]->currentinfo.node.loc.att.lvlh.utc = currentutc;
            att_set_lvlh(cnodes[j]->currentinfo.node.loc);
        }
        return 0;
    }
    for (uint16_t i=0; i<cnodes.size(); ++i)
    {
        update_target(&cnodes[i]->currentinfo);
    }
}

int32_t Simulator::Target()
{
    int32_t iretn = 0;
    // uint32_t minlevel = 10000;
    // Synchronize progress of all targets
    // update_target was already called for each node inside State::Propagate()
    for (uint16_t i=0; i<cnodes.size(); ++i)
    {
        for (uint16_t j=0; j<cnodes[i]->currentinfo.target.size(); ++j)
        {
            if (cnodes[i]->currentinfo.target[j].cover[0].count > targets[cnodes[i]->currentinfo.target[j].name].cover[0].count)
            {
                targets[cnodes[i]->currentinfo.target[j].name].cover[0].count = cnodes[i]->currentinfo.target[j].cover[0].count;
            }
            // if (cnodes[i]->currentinfo.target[j].elto > 0.)
            // {
            //     uint32_t mylevel = targets[cnodes[i]->currentinfo.target[j].name].cover[0].count / cnodes[i]->currentinfo.target[j].elto;
            //     if (mylevel < minlevel)
            //     {
            //         minlevel = mylevel;
            //     }
            // }
        }
    }

    // Pick current target for each Node
    for (uint16_t i=0; i<cnodes.size(); ++i)
    {
        uint32_t minlevel = 10000;
        cnodes[i]->currentinfo.target_idx = -1;
        for (uint16_t j=0; j<cnodes[i]->currentinfo.target.size(); ++j)
        {
            if (cnodes[i]->currentinfo.target[j].elto > 0.)
            {
                // Remain candidate if weighted count is low enough
                uint32_t mylevel = 0.01 * targets[cnodes[i]->currentinfo.target[j].name].cover[0].count / cnodes[i]->currentinfo.target[j].elto;
                if (mylevel < minlevel)
                {
                    // Remove from candidacy if previous Node has been targeted
                    if (i > 0)
                    {
                        for (uint16_t ii=i-1; ii<cnodes.size(); --ii)
                        {
                            if (cnodes[ii]->currentinfo.target_idx == j)
                            {
                                mylevel = minlevel;
                            }
                        }
                    }
                    if (mylevel < minlevel)
                    {
                        minlevel = mylevel;
                        cnodes[i]->currentinfo.target_idx = j;
                    }
                }
            }
            // if (cnodes[i]->currentinfo.target_idx < cnodes[i]->currentinfo.target.size())
            // {
            //     break;
            // }
        }
    }

    for (uint16_t i=0; i<cnodes.size(); ++i)
    {
        Vector torque;
        if (cnodes[i]->currentinfo.target_idx < cnodes[i]->currentinfo.target.size())
        {
            // torque = calc_control_torque(cnodes[i]->currentinfo.node.phys.maxtorque, cnodes[i]->currentinfo.node.phys.moi, cnodes[i]->currentinfo.target[cnodes[i]->currentinfo.target_idx].loc.att.icrf, cnodes[i]->currentinfo.node.loc.att.icrf);
            cnodes[i]->currentinfo.node.loc.att.icrf = cnodes[i]->currentinfo.target[cnodes[i]->currentinfo.target_idx].loc.att.icrf;
        }
        UpdateTorque(cnodes[i]->currentinfo.node.name, torque);
    }
    return iretn;
}

int32_t Simulator::Target(vector<vector<cosmosstruc> > &results)
{
    int32_t iretn = 0;
    uint32_t minlevel = -1;
    // for (uint32_t tstep=0; tstep<results.size(); ++tstep)
    // {
    //     for (uint16_t i=0; i<results[tstep].size(); ++i)
    //     {
    //         update_target(&results[tstep][i]);
    //         for (uint16_t j=0; j<results[tstep][i].target.size(); ++j)
    //         {
    //             if (results[tstep][i].target[j].elto > 0.087 && results[tstep][i].target[j].cover[0].count / cnodes[i]->currentinfo.target[j].elto < minlevel)
    //             {
    //                 minlevel = results[tstep][i].target[j].cover[0].count / cnodes[i]->currentinfo.target[j].elto;
    //             }
    //         }
    //     }
    // }

    for (uint32_t tstep=0; tstep<results.size(); ++tstep)
    {
        for (uint16_t i=0; i<results[tstep].size(); ++i)
        {
            update_target(&results[tstep][i]);
            results[tstep][i].target_idx = -1;
            double targetrange = 1e9;
            for (uint16_t j=0; j<results[tstep][i].target.size(); ++j)
            {
                if (results[tstep][i].target[j].elto > 0.087)
                {
                    bool taken = false;
                    for (uint16_t ii=i-1; ii<results[tstep].size(); --ii)
                    {
                        if (results[tstep][ii].target_idx == j || results[tstep][i].target[j].cover[0].count / cnodes[i]->currentinfo.target[j].elto > minlevel)
                        {
                            taken = true;
                        }
                    }
                    if (!taken && results[tstep][i].target[j].range < targetrange)
                    {
                        if (targetrange == 1e9)
                        {
                            ++iretn;
                        }
                        targetrange = results[tstep][i].target[j].range;
                        results[tstep][i].target_idx = j;
                    }
                }
            }
            if (results[tstep][i].target_idx < results[tstep][i].target.size())
            {
                results[tstep][i].target[results[tstep][i].target_idx].cover[0].count++;
                results[tstep][i].node.loc.att.geoc.s = q_drotate_between_rv(rv_unitz(), rv_sub(results[tstep][i].node.loc.pos.geoc.s, results[tstep][i].target[results[tstep][i].target_idx].loc.pos.geoc.s));
                results[tstep][i].node.loc.att.geoc.v = rv_zero();
                results[tstep][i].node.loc.att.geoc.a = rv_zero();
                att_set_geoc(results[tstep][i].node.loc);
            }
            else
            {
                results[tstep][i].node.loc.att.lvlh.s = q_eye();
                results[tstep][i].node.loc.att.lvlh.v = rv_zero();
                results[tstep][i].node.loc.att.lvlh.a = rv_zero();
                results[tstep][i].node.loc.att.lvlh.utc = currentutc;
                att_set_lvlh(results[tstep][i].node.loc);
            }
        }
    }
    return iretn;
}

int32_t Simulator::Metric()
{
    int32_t iretn = 0;
    for (uint16_t i=0; i<cnodes.size(); ++i)
    {
        iretn = update_metrics(&cnodes[i]->currentinfo);
        if (iretn < 0)
        {
            return iretn;
        }
    }
    return iretn;
}

int32_t Simulator::Metric(vector<vector<cosmosstruc> > &results)
{
    int32_t iretn = 0;
    for (uint16_t tstep=0; tstep<results.size(); ++tstep)
    {
        for (uint16_t i=0; i<results[tstep].size(); ++i)
        {
            iretn = update_metrics(&results[tstep][i]);
            if (iretn < 0)
            {
                return iretn;
            }
        }
    }
    return iretn;
}

int32_t Simulator::Thrust()
{
    // Calculate thrust
    for (uint16_t i=0; i<cnodes.size(); ++i)
    {
        // cnodes[i]->currentinfo.node.loc_req.pos.eci = cnodes[0]->currentinfo.node.loc.pos.eci;
        // cnodes[i]->currentinfo.node.loc_req.pos.geoc = cnodes[0]->currentinfo.node.loc.pos.geoc;
        // cnodes[i]->currentinfo.node.loc_req.pos.lvlh = cnodes[i]->currentinfo.node.loc.pos.lvlh;
        // pos_origin2lvlh(cnodes[i]->currentinfo.node.loc_req);
        if (length_rv(cnodes[i]->currentinfo.node.loc_req.pos.eci.s))
        {
            UpdatePush(cnodes[i]->currentinfo.node.name, Physics::ControlThrust(cnodes[i]->currentinfo.node.loc.pos.eci, cnodes[i]->currentinfo.node.loc_req.pos.eci, cnodes[i]->currentinfo.mass, cnodes[i]->currentinfo.devspec.thst[0].maxthrust/cnodes[i]->currentinfo.mass, dt));
            if (i)
            {
                cnodes[i]->currentinfo.node.loc.pos.eci.s += .1 * (cnodes[i]->currentinfo.node.loc_req.pos.eci.s - cnodes[i]->currentinfo.node.loc.pos.eci.s);
            }
        }
    }
    return 0;
}

int32_t Simulator::Formation(string type, double spacing)
{
    if (type == "nochange")
    {
        return 0;
    }
    else if (type == "hold")
    {
        // Hold each child at its offset from the mother when first seen
        // (normally its sats.dat placement), kept in the mother's LVLH frame
        // about the body she is closest to.
        if (m_holdOffset.size() != cnodes.size())
        {
            m_holdOffset.resize(cnodes.size());
            for (uint16_t i=1; i<cnodes.size(); ++i)
            {
                locstruc child = cnodes[i]->currentinfo.node.loc;
                locstruc mother = cnodes[0]->currentinfo.node.loc;
                pos_planec2lvlh(child, mother);
                m_holdOffset[i] = mother.pos.lvlh;
            }
        }
        for (uint16_t i=1; i<cnodes.size(); ++i)
        {
            locstruc mother = cnodes[0]->currentinfo.node.loc;
            mother.pos.lvlh = m_holdOffset[i];
            pos_lvlh2planec(mother, cnodes[i]->currentinfo.node.loc_req);
        }
        return 4;
    }
    else if (type == "string")
    {
        for (uint16_t i=1; i<cnodes.size(); ++i)
        {
            // The whole state: pos_origin2lvlh builds the LVLH axes from the velocity
            cnodes[i]->currentinfo.node.loc_req.pos.geoc = cnodes[0]->currentinfo.node.loc.pos.geoc;
            cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s = rv_zero();
            cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s.col[1] = -spacing * i;
            ric2lvlh(length_rv(cnodes[i]->currentinfo.node.loc.pos.geoc.s), cnodes[i]->currentinfo.node.loc_req.pos.lvlh, cnodes[i]->currentinfo.node.loc_req.pos.lvlh);
            pos_origin2lvlh(cnodes[i]->currentinfo.node.loc_req);
            pos_set_geoc(cnodes[i]->currentinfo.node.loc_req);
        }
        return 1;
    }
    else if (type == "diamond")
    {
        for (uint16_t i=1; i<cnodes.size(); ++i)
        {
            // The whole state: pos_origin2lvlh builds the LVLH axes from the velocity
            cnodes[i]->currentinfo.node.loc_req.pos.geoc = cnodes[0]->currentinfo.node.loc.pos.geoc;
            cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s = rv_zero();
            //            lvlh2ric(length_rv(cnodes[i]->currentinfo.node.loc.pos.geoc.s), cnodes[i]->currentinfo.node.loc_req.pos.lvlh, cnodes[i]->currentinfo.node.loc_req.pos.lvlh);
            if (i == 2)
            {
                cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s.col[1] = -spacing * 2.5;
                cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s.col[2] = -spacing * 1.5;
            }
            else if (i == 3)
            {
                cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s.col[1] = -spacing * 2.5;
                cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s.col[2] = spacing * 1.5;
            }
            else
            {
                cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s.col[1] = -spacing * i;
                cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s.col[2] = 0;
            }
            ric2lvlh(length_rv(cnodes[i]->currentinfo.node.loc.pos.geoc.s), cnodes[i]->currentinfo.node.loc_req.pos.lvlh, cnodes[i]->currentinfo.node.loc_req.pos.lvlh);
            pos_origin2lvlh(cnodes[i]->currentinfo.node.loc_req);
            pos_set_geoc(cnodes[i]->currentinfo.node.loc_req);
        }
        return 2;
    }
    else if (type == "spread")
    {
        for (uint16_t i=1; i<cnodes.size(); ++i)
        {
            // The whole state: pos_origin2lvlh builds the LVLH axes from the velocity
            cnodes[i]->currentinfo.node.loc_req.pos.geoc = cnodes[0]->currentinfo.node.loc.pos.geoc;
            cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s = rv_zero();
            cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s.col[1] = -spacing * i;
            //            lvlh2ric(length_rv(cnodes[i]->currentinfo.node.loc.pos.geoc.s), cnodes[i]->currentinfo.node.loc_req.pos.lvlh, cnodes[i]->currentinfo.node.loc_req.pos.lvlh);
            if (i%2)
            {
                cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s.col[2] = -spacing * uint16_t((i + 1) / 2);
            }
            else
            {
                cnodes[i]->currentinfo.node.loc_req.pos.lvlh.s.col[2] = spacing * uint16_t((i + 1) / 2);
            }
            ric2lvlh(length_rv(cnodes[i]->currentinfo.node.loc_req.pos.geoc.s), cnodes[i]->currentinfo.node.loc_req.pos.lvlh, cnodes[i]->currentinfo.node.loc_req.pos.lvlh);
            pos_origin2lvlh(cnodes[i]->currentinfo.node.loc_req);
            pos_set_geoc(cnodes[i]->currentinfo.node.loc_req);
        }
        return 3;
    }
    return 0;
}

int32_t Simulator::End()
{
    int32_t iretn = 0;
    for (auto &state : cnodes)
    {
        iretn = state->End();
    }
    return iretn;
}

int32_t Simulator::Run()
{
    RunState = State::Running;
    return error;
}

int32_t Simulator::Pause()
{
    RunState = State::Paused;
    return error;
}

Simulator::State Simulator::GetState()
{
    return RunState;
}

int32_t Simulator::GetNode(string name, Physics::State* &node)
{
    auto it = std::find_if(cnodes.begin(), cnodes.end(), [name](const Physics::State* state) {
        return state->currentinfo.node.name == name;
    });
    if (it == cnodes.end())
    {
        return 0;
    }
    node = *it;
    return 1;
}

Simulator::StateList::iterator Simulator::GetNode(string name)
{
    auto node = std::find_if(cnodes.begin(), cnodes.end(), [name](const Physics::State* state) {
        return state->currentinfo.node.name == name;
    });
    return node;
}

Simulator::StateList::const_iterator Simulator::GetNode(string name) const
{
    auto node = std::find_if(cnodes.begin(), cnodes.end(), [name](const Physics::State* state) {
        return state->currentinfo.node.name == name;
    });
    return node;
}

Simulator::StateList::const_iterator Simulator::GetEnd() const
{
    return cnodes.end();
}

void Simulator::SetFormation(string type, double spacing)
{
    m_formationType    = type;
    m_formationSpacing = spacing;
}

//! Angle to slew this step toward a pointing error of angle
//! Bang-bang slew time t = 2*sqrt(angle * I_min / tau_max), or a nominal 3 deg/s
//! when the node has no torque limit.
double Simulator::slewStepAngle(const Physics::State &state, double angle, double dt)
{
    double maxtorque  = state.currentinfo.node.phys.maxtorque;
    const Vector &moi = state.currentinfo.node.phys.moi;
    if (maxtorque > 0.)
    {
        double I_min   = std::min({moi.x, moi.y, moi.z});
        double t_slew  = 2.0 * std::sqrt(angle * I_min / maxtorque);
        return angle * std::min(1.0, dt / std::max(t_slew, dt));
    }
    return std::min(angle, RADOF(3.) * dt);
}

//! Set the node's ICRF attitude and keep it across steps
//! The angular velocity is the rate the attitude propagator left (the LVLH
//! frame's, carrying the last attitude along) plus the rate of this step's slew
//! from that attitude to icrf over dt [s].
void Simulator::setSlewedAttitude(Physics::State &state, const quaternion &icrf, double dt, double currentutc)
{
    qatt &catt = state.currentinfo.node.loc.att.icrf;
    if (dt > 0.)
    {
        // Rotation of the body axes in ICRF over the step: conj(icrf) = d * conj(s)
        quaternion d = q_fmult(q_conjugate(icrf), catt.s);
        if (d.w < 0.)
        {
            d.w = -d.w;
            d.d.x = -d.d.x;
            d.d.y = -d.d.y;
            d.d.z = -d.d.z;
        }
        double sinHalf = std::sqrt(d.d.x*d.d.x + d.d.y*d.d.y + d.d.z*d.d.z);
        if (sinHalf > 0.)
        {
            double rate = 2. * std::atan2(sinHalf, d.w) / dt / sinHalf;
            catt.v = rv_add(catt.v, rvector(d.d.x * rate, d.d.y * rate, d.d.z * rate));
        }
    }
    catt.s   = icrf;
    catt.utc = currentutc;
    att_set_icrf(state.currentinfo.node.loc);
    // The LVLH attitude propagator rebuilds icrf from loc_req.att.lvlh every step,
    // so persist the slewed attitude there or it is overwritten next step.
    att_icrf2lvlh(state.currentinfo.node.loc);
    state.currentinfo.node.loc_req.att.lvlh.s = state.currentinfo.node.loc.att.lvlh.s;
}

//! Slew the whole attitude toward targetIcrf (slerp at the slewStepAngle rate limit)
void Simulator::slewAttitude(Physics::State &state, const quaternion &targetIcrf, double dt, double currentutc)
{
    quaternion q0 = state.currentinfo.node.loc.att.icrf.s;
    quaternion q1 = targetIcrf;
    double cosHalf = q0.w*q1.w + q0.d.x*q1.d.x + q0.d.y*q1.d.y + q0.d.z*q1.d.z;
    // q and -q are the same attitude: take the short way
    if (cosHalf < 0.)
    {
        cosHalf = -cosHalf;
        q1.w = -q1.w;
        q1.d.x = -q1.d.x;
        q1.d.y = -q1.d.y;
        q1.d.z = -q1.d.z;
    }
    cosHalf = std::min(1.0, cosHalf);
    double angle = 2. * std::acos(cosHalf);
    if (angle < 1e-9) return;

    double stepAng = slewStepAngle(state, angle, dt);
    if (stepAng >= angle || angle < 1e-6)
    {
        setSlewedAttitude(state, q1, dt, currentutc);
        return;
    }
    double sinHalf = std::sin(angle / 2.);
    double f  = stepAng / angle;
    double s0 = std::sin((1. - f) * angle / 2.) / sinHalf;
    double s1 = std::sin(f * angle / 2.) / sinHalf;
    quaternion q;
    q.w   = s0*q0.w   + s1*q1.w;
    q.d.x = s0*q0.d.x + s1*q1.d.x;
    q.d.y = s0*q0.d.y + s1*q1.d.y;
    q.d.z = s0*q0.d.z + s1*q1.d.z;
    normalize_q(&q);
    setSlewedAttitude(state, q, dt, currentutc);
}

//! Unit ICRF direction from the node for one aim of request_adcs point
//! target and station come from the node's target list (as update_target() left
//! it); nearest is the closest one whose zenith angle, seen from it, is within
//! the limit, and for a station whose link is not blocked by the Moon.
//! \return Zero, or negative if there is no such direction.
int32_t Simulator::aimDirection(const cosmosstruc &info, const Support::Agent::AdcsAim &aim, rvector &dir)
{
    const locstruc &loc = info.node.loc;
    bool lunar = (Physics::closest_body(loc) == COSMOS_MOON);
    if (aim.mode == "sun")
    {
        dir = rv_sub(rv_smult(-1., loc.pos.extra.sun2earth.s), loc.pos.eci.s);
    }
    else if (aim.mode == "earth")
    {
        dir = rv_smult(-1., loc.pos.eci.s);
    }
    else if (aim.mode == "nadir")
    {
        dir = rv_smult(-1., lunar ? loc.pos.sci.s : loc.pos.eci.s);
    }
    else if (aim.mode == "velocity")
    {
        dir = lunar ? loc.pos.sci.v : loc.pos.eci.v;
    }
    else if (aim.mode == "target" || aim.mode == "station")
    {
        const targetstruc *best = nullptr;
        double bestrange = 0.;
        for (const targetstruc &t : info.target)
        {
            if (aim.mode == "target" ? t.type != NODE_TYPE_TARGET : (t.type != NODE_TYPE_GROUNDSTATION && t.type != NODE_TYPE_MOC))
            {
                continue;
            }
            if (aim.name == "nearest")
            {
                if (DPI2 - t.elto > aim.zenith || (aim.mode == "station" && !Physics::comm_link_clear(loc, t)))
                {
                    continue;
                }
            }
            else if (t.name != aim.name)
            {
                continue;
            }
            double range = length_rv(rv_sub(t.loc.pos.eci.s, loc.pos.eci.s));
            if (best == nullptr || range < bestrange)
            {
                best = &t;
                bestrange = range;
            }
        }
        if (best == nullptr)
        {
            return GENERAL_ERROR_ARGS;
        }
        dir = rv_sub(best->loc.pos.eci.s, loc.pos.eci.s);
    }
    else
    {
        return GENERAL_ERROR_ARGS;
    }
    if (length_rv(dir) < 1e-9)
    {
        return GENERAL_ERROR_ARGS;
    }
    dir = rv_normal(dir);
    return 0;
}

//! ICRF attitude for a commanded pointing, at the node's current position
//! An aim with no usable align keeps the roll about the aimed axis: another body
//! axis is brought as close as it can be to where it points now.
//! \return Zero, or negative if there is nothing to point at (the node holds).
int32_t Simulator::commandedAttitude(const cosmosstruc &info, const AttitudeCommand &command, double currentutc, quaternion &icrf)
{
    if (command.mode == "aim")
    {
        rvector d1;
        int32_t iretn = aimDirection(info, command.aim, d1);
        if (iretn < 0)
        {
            return iretn;
        }
        rvector b1 = rv_normal(command.aim.axis);
        rvector b2, d2;
        bool aligned = false;
        if (!command.align.mode.empty() && aimDirection(info, command.align, d2) >= 0 && fabs(dot_rv(d1, d2)) < .999)
        {
            b2 = rv_normal(command.align.axis);
            aligned = true;
        }
        for (const rvector &axis : {rv_unitx(), rv_unity(), rv_unitz()})
        {
            if (aligned)
            {
                break;
            }
            d2 = transform_q(q_conjugate(info.node.loc.att.icrf.s), axis);
            if (fabs(dot_rv(axis, b1)) < .9 && fabs(dot_rv(d2, d1)) < .999)
            {
                b2 = axis;
                aligned = true;
            }
        }
        // Body and ICRF triads. q_dcm2quaternion_rm() of W * B^T (body to ICRF)
        // gives the quaternion with transform_q(icrf, d1) = b1.
        rvector t2 = rv_normal(rv_cross(b1, b2));
        rvector w2 = rv_normal(rv_cross(d1, d2));
        rmatrix bt(b1, t2, rv_cross(b1, t2));
        rmatrix wt(d1, w2, rv_cross(d1, w2));
        icrf = q_dcm2quaternion_rm(rm_mmult(rm_transpose(wt), bt));
        normalize_q(&icrf);
        return 0;
    }

    locstruc tloc = info.node.loc;
    qatt catt;
    catt.utc = currentutc;
    catt.s = command.q;
    int32_t iretn;
    if (command.frame == "topo")
    {
        tloc.att.topo = catt;
        iretn = att_set_topo(tloc);
    }
    else if (command.frame == "lvlh")
    {
        tloc.att.lvlh = catt;
        iretn = att_set_lvlh(tloc);
    }
    else if (command.frame == "geoc")
    {
        tloc.att.geoc = catt;
        iretn = att_set_geoc(tloc);
    }
    else if (command.frame == "selc")
    {
        tloc.att.selc = catt;
        iretn = att_set_selc(tloc);
    }
    else
    {
        tloc.att.icrf = catt;
        iretn = att_set_icrf(tloc);
    }
    if (iretn < 0)
    {
        return iretn;
    }
    icrf = tloc.att.icrf.s;
    return 0;
}

// Per-step attitude control: slew each node toward its commanded attitude
// (request_adcs point) using the bang-bang rate limit. There is no automatic
// pointing: a node with no command holds its attitude. Called from Propagate()
// when enabletorque is true.
void Simulator::AttitudeStep()
{
    for (auto &state : cnodes)
    {
        auto command = attcommands.find(state->currentinfo.node.name);
        if (command == attcommands.end())
        {
            continue;
        }
        quaternion target;
        if (commandedAttitude(state->currentinfo, command->second, currentutc, target) >= 0)
        {
            slewAttitude(*state, target, dt, currentutc);
        }
    }
}

int32_t Simulator::UpdatePush(string name, Vector fpush)
{
    auto node = GetNode(name);
    if (node == cnodes.end())
    {
        return 0;
    }
    (*node)->currentinfo.node.phys.fpush = fpush;
    return 1;
}

int32_t Simulator::UpdateThrust(string name, Vector thrust)
{
    auto node = GetNode(name);
    if (node == cnodes.end())
    {
        return 0;
    }
    (*node)->currentinfo.node.phys.thrust = thrust;
    return 1;
}

int32_t Simulator::UpdateTorque(string name, Vector torque)
{
    auto node = GetNode(name);
    if (node == cnodes.end())
    {
        return 0;
    }
    (*node)->currentinfo.node.phys.ftorque = torque;
    return 1;
}

Simulator::StateList Simulator::GetNodes() {
    return cnodes;
}

}
}
