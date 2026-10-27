#include "physics/l0simclass.h"
#include "support/demlib.h"
#include "support/envi.h"
#include "support/jsonlib.h"
#include "agent/agentclass.h"
#include <fstream>
#include <random>

namespace Cosmos
{
    namespace Physics
    {
        namespace
        {
            constexpr double PLANCK_H = 6.62607015e-34;
            constexpr double LIGHT_C = 2.99792458e8;

            //! Solar spectral irradiance at 1 AU [W/m²/nm], approximately ASTM E-490
            double solar_irradiance(int32_t nm)
            {
                static const map<int32_t, double> table = {
                    {300, 0.51}, {532, 1.86}, {690, 1.42}, {850, 0.96},
                    {1064, 0.64}, {1350, 0.40}, {1560, 0.27}, {1650, 0.23}
                };
                auto it = table.lower_bound(nm);
                if (it == table.end())
                {
                    return table.rbegin()->second;
                }
                return it->second;
            }

            double number_or(const json11::Json &j, double fallback)
            {
                return j.is_number() ? j.number_value() : fallback;
            }

            quaternion q_slerp(quaternion q0, quaternion q1, double tau)
            {
                double dot = q0.d.x * q1.d.x + q0.d.y * q1.d.y + q0.d.z * q1.d.z + q0.w * q1.w;
                if (dot < 0.)
                {
                    q1 = q_smult(-1., q1);
                    dot = -dot;
                }
                double a0 = 1. - tau;
                double a1 = tau;
                if (dot < .9995)
                {
                    double theta = acos(dot);
                    double sint = sin(theta);
                    a0 = sin((1. - tau) * theta) / sint;
                    a1 = sin(tau * theta) / sint;
                }
                quaternion q = q_add(q_smult(a0, q0), q_smult(a1, q1));
                double norm = sqrt(q.d.x * q.d.x + q.d.y * q.d.y + q.d.z * q.d.z + q.w * q.w);
                return q_smult(1. / norm, q);
            }

            string fixed(double value, int32_t decimals)
            {
                char text[64];
                snprintf(text, sizeof(text), "%.*f", decimals, value);
                return text;
            }

            template <typename T> string envi_list(const vector<T> &items, std::function<string(const T &)> text)
            {
                string out = "{";
                for (size_t i = 0; i < items.size(); ++i)
                {
                    out += (i ? ", " : "") + text(items[i]);
                }
                return out + "}";
            }
        }

        int32_t L0Sim::Load(string realm, string node, string instrument)
        {
            this->realm = realm;
            this->node = node;
            this->instrument = instrument;
            ifstream nodefile(get_realmdir(realm) + "/nodes/" + node + ".json");
            if (!nodefile.is_open())
            {
                return GENERAL_ERROR_OPEN;
            }
            string contents((std::istreambuf_iterator<char>(nodefile)), std::istreambuf_iterator<char>());
            string error;
            json11::Json jnode = json11::Json::parse(contents, error);
            if (!error.empty())
            {
                return JSON_ERROR_SCAN;
            }
            const json11::Json &jinst = jnode["instruments"][instrument];
            if (!jinst.is_object())
            {
                return JSON_ERROR_NOENTRY;
            }

            baseline = jinst["baseline"].string_value();
            provisional = jinst["provisional"].int_value() != 0;
            aperture = jinst["aperture"].number_value();
            samples = jinst["l0"]["samples"].int_value();
            transmission = number_or(jinst["forward"]["transmission"], .5);
            dem_iterations = number_or(jinst["forward"]["dem_iterations"], 3.);

            cameras.clear();
            for (const auto &jc : jinst["cameras"].object_items())
            {
                const json11::Json &phys = jc.second["physical"];
                const json11::Json &acq = jc.second["acquisition"];
                Camera cam;
                cam.name = jc.first;
                cam.piece = jc.second["piece"].string_value();
                cam.detector = jc.second["detector"].string_value();
                cam.cols = phys["cols"].int_value();
                cam.rows = phys["rows"].int_value();
                cam.pitch = phys["pitch"].number_value();
                cam.ifov = phys["ifov"].number_value();
                cam.read_noise = phys["read_noise"].number_value();
                cam.dark_current = phys["dark_current"].number_value();
                cam.full_well = phys["full_well"].number_value();
                cam.bits = phys["bits"].int_value();
                for (const auto &jq : phys["qe"].object_items())
                {
                    cam.qe[stoi(jq.first)] = jq.second.number_value();
                }
                cam.sp_across = acq["superpixel"][0].int_value();
                cam.sp_along = acq["superpixel"][1].int_value();
                cam.swath_cols = acq["swath_cols"].int_value();
                cam.first_col = acq["first_col"].int_value();
                for (const auto &js : acq["stripes"].object_items())
                {
                    Stripe stripe;
                    stripe.along = js.second["along"].number_value();
                    stripe.first_row = js.second["first_row"].int_value();
                    cam.stripes[stoi(js.first)] = stripe;
                }
                cameras.push_back(cam);
            }

            // Every channel some camera has a stripe for
            bands.clear();
            for (size_t ic = 0; ic < cameras.size(); ++ic)
            {
                for (const auto &stripe : cameras[ic].stripes)
                {
                    Band band;
                    band.nm = stripe.first;
                    band.camera = ic;
                    band.stripe = stripe.second;
                    string key = std::to_string(band.nm);
                    band.albedo = number_or(jinst["forward"]["albedo"][key], .12);
                    band.bandwidth = number_or(jinst["forward"]["bandwidth"][key], 20.);
                    band.irradiance = number_or(jinst["forward"]["irradiance"][key], solar_irradiance(band.nm));
                    bands[band.nm] = band;
                }
            }

            modes.clear();
            for (const auto &jm : jinst["modes"].object_items())
            {
                Mode mode;
                mode.name = jm.first;
                mode.event = jm.second["event"].string_value();
                mode.rate = jm.second["rate"].number_value();
                mode.inttime = jm.second["inttime"].number_value();
                const json11::Json &jchan = jm.second[jm.first == "active" ? "lasers" : "channels"];
                for (const auto &jn : jchan.array_items())
                {
                    mode.channels.push_back(jn.int_value());
                }
                modes[mode.name] = mode;
            }
            if (cameras.empty() || bands.empty() || !samples || aperture <= 0.)
            {
                return GENERAL_ERROR_ARGS;
            }
            return cameras.size();
        }

        int32_t L0Sim::LoadTelemetry(double utcbegin, double utcend)
        {
            knots.clear();
            for (const archivefile &af : archive_list(realm, node, "telemetry", utcbegin, utcend, "telemetry"))
            {
                ifstream file(af.path);
                string line;
                while (getline(file, line))
                {
                    string error;
                    json11::Json j = json11::Json::parse(line, error);
                    if (!error.empty() || j["jtype"].string_value() != "soh" || j["scipos"].is_null())
                    {
                        continue;
                    }
                    Knot knot;
                    knot.utc = j["utc"].number_value();
                    if (knot.utc < utcbegin || knot.utc > utcend)
                    {
                        continue;
                    }
                    Convert::cartpos pos;
                    pos.from_json(j["scipos"].dump());
                    Convert::qatt att;
                    att.from_json(j["alphaatt"].dump());
                    knot.s = pos.s;
                    knot.v = pos.v;
                    knot.q = att.s;
                    knot.w = att.v;
                    knots.push_back(knot);
                }
            }
            sort(knots.begin(), knots.end(), [](const Knot &a, const Knot &b) { return a.utc < b.utc; });
            // Overlapping runs can archive the same time twice
            knots.erase(unique(knots.begin(), knots.end(), [](const Knot &a, const Knot &b) { return fabs(a.utc - b.utc) < 1e-9; }), knots.end());
            return knots.size();
        }

        vector<L0Sim::Tile> L0Sim::LoadTiles(double utcbegin, double utcend)
        {
            vector<Tile> tiles;
            // A tile is filed by the step that ended it, so its file can start after it
            for (const archivefile &af : archive_list(realm, node, "telemetry", utcbegin, utcend + 1./96., "event"))
            {
                ifstream file(af.path);
                string line;
                while (getline(file, line))
                {
                    string evnode;
                    eventstruc event;
                    if (json_event_payload_load(evnode, event, line) < 0 || event.type != EVENT_TYPE_REQUEST || event.value < 1.)
                    {
                        continue;
                    }
                    if (event.utc < utcbegin || event.utc > utcend)
                    {
                        continue;
                    }
                    for (const auto &mode : modes)
                    {
                        if (mode.second.event.empty() || mode.second.event != event.name)
                        {
                            continue;
                        }
                        Tile tile;
                        uint32_t frames;
                        uint32_t tilesize;
                        if (Support::Agent::parse_camera_snap(event.data, tile.camera, frames, tile.rate, tile.inttime, tilesize) < 0 || tile.rate <= 0.)
                        {
                            continue;
                        }
                        tile.mode = mode.first;
                        tile.name = event.name;
                        tile.request = event.data;
                        tile.utc = event.utc;
                        tile.frames = static_cast<uint32_t>(event.value + .5);
                        tiles.push_back(tile);
                    }
                }
            }
            sort(tiles.begin(), tiles.end(), [](const Tile &a, const Tile &b) { return a.utc < b.utc; });
            tiles.erase(unique(tiles.begin(), tiles.end(), [](const Tile &a, const Tile &b) { return a.mode == b.mode && fabs(a.utc - b.utc) < 1e-9; }), tiles.end());
            return tiles;
        }

        int32_t L0Sim::Interpolate(double utc, rvector &s, quaternion &q)
        {
            if (knots.size() < 2 || utc < knots.front().utc || utc > knots.back().utc)
            {
                return GENERAL_ERROR_OUTOFRANGE;
            }
            auto it = upper_bound(knots.begin(), knots.end(), utc, [](double t, const Knot &k) { return t < k.utc; });
            if (it == knots.end())
            {
                --it;
            }
            const Knot &k1 = *it;
            const Knot &k0 = *(it - 1);
            double h = 86400. * (k1.utc - k0.utc);
            // A gap in the telemetry: don't guess across it
            if (h <= 0. || h > 600.)
            {
                return GENERAL_ERROR_OUTOFRANGE;
            }
            double tau = 86400. * (utc - k0.utc) / h;
            double tau2 = tau * tau;
            double tau3 = tau2 * tau;
            double h00 = 2. * tau3 - 3. * tau2 + 1.;
            double h10 = tau3 - 2. * tau2 + tau;
            double h01 = -2. * tau3 + 3. * tau2;
            double h11 = tau3 - tau2;
            s = rv_add(rv_add(rv_smult(h00, k0.s), rv_smult(h10 * h, k0.v)), rv_add(rv_smult(h01, k1.s), rv_smult(h11 * h, k1.v)));
            q = q_slerp(k0.q, k1.q, tau);
            return 0;
        }

        int32_t L0Sim::Generate(const Tile &tile, bool overwrite)
        {
            auto mode = modes.find(tile.mode);
            if (mode == modes.end())
            {
                return GENERAL_ERROR_ARGS;
            }
            string dir = get_archivedir(realm, node, "data", tile.utc, true);
            if (dir.empty())
            {
                return GENERAL_ERROR_OPEN;
            }
            string path = dir + "/" + archive_name(tile.mode, tile.utc, "img");
            if (!overwrite && data_isfile(path))
            {
                return 0;
            }
            if (tile.mode == "passive")
            {
                return PassiveTile(tile, mode->second, path);
            }
            // Active mode is a later work item
            return 0;
        }

        int32_t L0Sim::PassiveTile(const Tile &tile, const Mode &mode, string path)
        {
            vector<const Band *> tilebands;
            for (int32_t nm : mode.channels)
            {
                auto band = bands.find(nm);
                if (band != bands.end())
                {
                    tilebands.push_back(&band->second);
                }
            }
            if (tilebands.empty() || !tile.frames)
            {
                return GENERAL_ERROR_ARGS;
            }

            size_t lines = tile.frames;
            size_t nb = tilebands.size();
            vector<int32_t> data(lines * nb * samples, 0);
            // Reproducible: the same tile gives the same noise
            std::mt19937_64 rng(static_cast<uint64_t>(llround(tile.utc * 86400. * 1000.)));
            std::normal_distribution<double> gauss(0., 1.);

            double area = DPI * aperture * aperture / 4.;
            double demres = cameras[tilebands[0]->camera].ifov * cameras[tilebands[0]->camera].sp_across * 40000. / RMOONM;
            // Reference band for the footprint: the stripe nearest the boresight
            size_t refband = 0;
            for (size_t ib = 1; ib < nb; ++ib)
            {
                if (fabs(tilebands[ib]->stripe.along) < fabs(tilebands[refband]->stripe.along))
                {
                    refband = ib;
                }
            }
            vector<double> footprint;
            size_t missinglines = 0;
            misses = 0;

            for (size_t line = 0; line < lines; ++line)
            {
                // Geometry at mid-exposure
                double utc = tile.utc + (line / tile.rate + tile.inttime / 2.) / 86400.;
                rvector sc;
                quaternion q;
                if (Interpolate(utc, sc, q) < 0)
                {
                    ++missinglines;
                    continue;
                }
                Convert::locstruc loc;
                Convert::pos_extra(utc, loc);
                rvector sun = rv_smult(-1., loc.pos.extra.sun2moon.s);
                quaternion body2icrf = q_conjugate(q);

                for (size_t ib = 0; ib < nb; ++ib)
                {
                    const Band &band = *tilebands[ib];
                    const Camera &cam = cameras[band.camera];
                    double qe = cam.qe.count(band.nm) ? cam.qe.at(band.nm) : .5;
                    double photon = PLANCK_H * LIGHT_C / (band.nm * 1e-9);
                    double npix = cam.sp_across * cam.sp_along;
                    double gain = cam.full_well / pow(2., cam.bits);
                    double darke = npix * cam.dark_current * tile.inttime;
                    double reade2 = npix * cam.read_noise * cam.read_noise;
                    // Electrons per physical pixel per W/m²/sr/nm of radiance
                    double response = band.bandwidth * area * cam.ifov * cam.ifov * transmission * qe * tile.inttime / photon;

                    for (size_t is = 0; is < samples; ++is)
                    {
                        // Body frame: boresight +Z, along-track +X (ahead), cross-track +Y
                        double col = cam.first_col + (is + .5) * cam.sp_across;
                        double cross = (col - cam.cols / 2.) * cam.ifov;
                        rvector dir = rv_normal(rvector(tan(band.stripe.along), tan(cross), 1.));
                        dir = transform_q(body2icrf, dir);

                        // Intersect with the Moon, refined with the DEM
                        double radius = RMOONM;
                        rvector point;
                        bool hit = false;
                        double lat = 0.;
                        double lon = 0.;
                        for (uint16_t it = 0; it <= dem_iterations; ++it)
                        {
                            double b = dot_rv(sc, dir);
                            double c = dot_rv(sc, sc) - radius * radius;
                            double disc = b * b - c;
                            if (disc < 0.)
                            {
                                hit = false;
                                break;
                            }
                            double range = -b - sqrt(disc);
                            if (range <= 0.)
                            {
                                hit = false;
                                break;
                            }
                            hit = true;
                            point = rv_add(sc, rv_smult(range, dir));
                            rvector selc = rv_mmult(loc.pos.extra.j2s, point);
                            lat = asin(selc.col[2] / length_rv(selc));
                            lon = atan2(selc.col[1], selc.col[0]);
                            if (it < dem_iterations)
                            {
                                radius = RMOONM + Convert::map_dem_alt(COSMOS_MOON, lon, lat, demres);
                            }
                        }

                        double signal = 0.;
                        if (hit)
                        {
                            rvector tosun = rv_sub(sun, point);
                            double cosi = dot_rv(rv_normal(point), rv_normal(tosun));
                            if (cosi > 0.)
                            {
                                double au = length_rv(tosun) / AU;
                                double radiance = band.albedo * band.irradiance * cosi / DPI / (au * au);
                                signal = npix * std::min(cam.full_well, radiance * response);
                            }
                        }
                        else
                        {
                            ++misses;
                        }
                        if (ib == refband && (is == 0 || is == samples - 1) && (line == 0 || line == lines - 1))
                        {
                            footprint.push_back(DEGOF(lat));
                            footprint.push_back(DEGOF(lon));
                        }
                        double electrons = signal + gauss(rng) * sqrt(signal + darke + reade2);
                        double dn = round(electrons / gain);
                        dn = std::max(static_cast<double>(INT32_MIN), std::min(static_cast<double>(INT32_MAX), dn));
                        // write_envi_data() takes band-sequential data and writes the header's interleave
                        data[(ib * lines + line) * samples + is] = static_cast<int32_t>(dn);
                    }
                }
            }
            if (missinglines == lines)
            {
                return GENERAL_ERROR_OUTOFRANGE;
            }

            envi_hdr hdr;
            hdr.basename = path;
            hdr.columns = samples;
            hdr.rows = lines;
            hdr.planes = nb;
            hdr.datatype = DT_LONG;
            hdr.interleave = BIL;
            hdr.byteorder = BO_INTEL;
            hdr.x0 = hdr.y0 = 0.;
            hdr.xmpp = hdr.ympp = 1.;
            hdr.description = "Simulated " + instrument + " " + tile.mode + " L0, " + realm + "/" + node;
            for (const Band *band : tilebands)
            {
                hdr.band_name.push_back(cameras[band->camera].name + " " + std::to_string(band->nm) + " nm");
                hdr.wavelength.push_back(band->nm);
            }
            vector<const Band *> bl = tilebands;
            auto add = [&hdr](string key, string value) { hdr.keys.push_back(key); hdr.values.push_back(value); };
            add("wavelength units", "Nanometers");
            add("simulated", "1");
            add("provisional", provisional ? "1" : "0");
            add("baseline", "{" + baseline + "}");
            add("instrument", instrument);
            add("mode", tile.mode);
            add("realm", realm);
            add("node", node);
            add("request", "{" + tile.request + "}");
            add("time system", "UTC MJD");
            add("first line utc", fixed(tile.utc, 10));
            add("line period", fixed(1. / tile.rate, 9));
            add("integration time", fixed(tile.inttime, 6));
            add("missing lines", std::to_string(missinglines));
            add("missed samples", std::to_string(misses));
            add("body frame", "{boresight +Z, along-track +X (ahead), cross-track +Y}");
            add("band camera", envi_list<const Band *>(bl, [this](const Band *const &b) { return cameras[b->camera].name; }));
            add("band piece", envi_list<const Band *>(bl, [this](const Band *const &b) { return cameras[b->camera].piece; }));
            add("band ifov", envi_list<const Band *>(bl, [this](const Band *const &b) { return fixed(cameras[b->camera].ifov, 9); }));
            add("band superpixel", envi_list<const Band *>(bl, [this](const Band *const &b) { return std::to_string(cameras[b->camera].sp_across) + "x" + std::to_string(cameras[b->camera].sp_along); }));
            add("band first col", envi_list<const Band *>(bl, [this](const Band *const &b) { return std::to_string(cameras[b->camera].first_col); }));
            add("band first row", envi_list<const Band *>(bl, [](const Band *const &b) { return std::to_string(b->stripe.first_row); }));
            add("band along", envi_list<const Band *>(bl, [](const Band *const &b) { return fixed(b->stripe.along, 6); }));
            add("band gain", envi_list<const Band *>(bl, [this](const Band *const &b) { return fixed(cameras[b->camera].full_well / pow(2., cameras[b->camera].bits), 6); }));
            add("forward model", "{constant albedo per band, Lambert, spherical normal, DEM lola_16}");
            add("band albedo", envi_list<const Band *>(bl, [](const Band *const &b) { return fixed(b->albedo, 3); }));
            add("reference frame", "{Moon, selenocentric ICRF position, ICRF to body attitude}");
            add("knot fields", "{utc, sx, sy, sz, vx, vy, vz, qx, qy, qz, qw, wx, wy, wz}");
            add("knots", KnotsText(tile.utc, tile.utc + (lines / tile.rate + tile.inttime) / 86400.));
            add("footprint", envi_list<double>(footprint, [](const double &v) { return fixed(v, 5); }));
            add("footprint fields", "{first line: lat, lon of first sample, lat, lon of last sample; last line: same}");
            int32_t iretn = write_envi_data(hdr, reinterpret_cast<uint8_t *>(data.data()));
            if (iretn < 0)
            {
                return iretn;
            }
            return lines - missinglines;
        }

        //! The knots from the last at or before utcbegin to the first at or after utcend, as an ENVI list
        string L0Sim::KnotsText(double utcbegin, double utcend)
        {
            vector<string> values;
            for (size_t i = 0; i < knots.size(); ++i)
            {
                bool before = i + 1 < knots.size() && knots[i + 1].utc <= utcbegin;
                bool after = i > 0 && knots[i - 1].utc >= utcend;
                if (before || after)
                {
                    continue;
                }
                const Knot &k = knots[i];
                values.push_back(fixed(k.utc, 10));
                for (double v : {k.s.col[0], k.s.col[1], k.s.col[2], k.v.col[0], k.v.col[1], k.v.col[2]})
                {
                    values.push_back(fixed(v, 3));
                }
                for (double v : {k.q.d.x, k.q.d.y, k.q.d.z, k.q.w, k.w.col[0], k.w.col[1], k.w.col[2]})
                {
                    values.push_back(fixed(v, 9));
                }
            }
            return envi_list<string>(values, [](const string &v) { return v; });
        }

        int32_t L0Sim::Run(double utcbegin, double utcend, bool overwrite)
        {
            int32_t iretn = LoadTelemetry(utcbegin - 1./1440., utcend + 1./96.);
            if (iretn < 2)
            {
                return iretn < 0 ? iretn : GENERAL_ERROR_EMPTY;
            }
            int32_t count = 0;
            for (const Tile &tile : LoadTiles(utcbegin, utcend))
            {
                iretn = Generate(tile, overwrite);
                if (iretn < 0)
                {
                    fprintf(stderr, "L0 %s tile at %.8f: %s\n", tile.mode.c_str(), tile.utc, cosmos_error_string(iretn).c_str());
                    continue;
                }
                if (iretn > 0)
                {
                    ++count;
                }
            }
            return count;
        }
    }
}
