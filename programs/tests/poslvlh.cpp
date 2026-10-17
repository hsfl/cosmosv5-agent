#include "support/configCosmos.h"
#include "support/convertlib.h"
#include "support/jsonlib.h"
#include "physics/physicslib.h"

int main(int argc, char *argv[])
{
    Convert::locstruc loc;
    cosmosstruc* cinfo = json_init();

    Physics::orbit_init_shape(0, 1., currentmjd(), 400000, RADOF(56.), 0., cinfo);
    Convert::cartpos geoc = cinfo->node.loc.pos.geoc;

    cinfo->node.loc.pos.lvlh.s.col[0] = 0.;
    cinfo->node.loc.pos.lvlh.s.col[1] = 0.;
    cinfo->node.loc.pos.lvlh.s.col[2] = 0.;
    if (argc > 1)
    {
        cinfo->node.loc.pos.lvlh.s.col[0] = atof(argv[1]);
    }
    if (argc > 2)
    {
        cinfo->node.loc.pos.lvlh.s.col[1] = atof(argv[2]);
    }
    if (argc > 3)
    {
        cinfo->node.loc.pos.lvlh.s.col[2] = atof(argv[3]);
    }

    printf("%f %f %f\n", cinfo->node.loc.pos.lvlh.s.col[0], cinfo->node.loc.pos.lvlh.s.col[1], cinfo->node.loc.pos.lvlh.s.col[2]);
    printf("%f %f %f\n", cinfo->node.loc.pos.geoc.s.col[0]-geoc.s.col[0], cinfo->node.loc.pos.geoc.s.col[1]-geoc.s.col[1], cinfo->node.loc.pos.geoc.s.col[2]-geoc.s.col[2]);

    Convert::pos_origin2lvlh(cinfo->node.loc);

    printf("%f %f %f\n", cinfo->node.loc.pos.lvlh.s.col[0], cinfo->node.loc.pos.lvlh.s.col[1], cinfo->node.loc.pos.lvlh.s.col[2]);
    printf("%f %f %f\n", cinfo->node.loc.pos.geoc.s.col[0]-geoc.s.col[0], cinfo->node.loc.pos.geoc.s.col[1]-geoc.s.col[1], cinfo->node.loc.pos.geoc.s.col[2]-geoc.s.col[2]);

    Convert::pos_lvlh2origin(cinfo->node.loc);

    printf("%f %f %f\n", cinfo->node.loc.pos.lvlh.s.col[0], cinfo->node.loc.pos.lvlh.s.col[1], cinfo->node.loc.pos.lvlh.s.col[2]);
    printf("%f %f %f\n", cinfo->node.loc.pos.geoc.s.col[0]-geoc.s.col[0], cinfo->node.loc.pos.geoc.s.col[1]-geoc.s.col[1], cinfo->node.loc.pos.geoc.s.col[2]-geoc.s.col[2]);

    // Round trips through the planet-centered LVLH pairs: point -> offset -> point
    int failures = 0;
    rvector offset = rvector(100., -2000., 30.);
    auto roundtrip = [&](const char *name, Convert::locstruc base, bool moon, bool planec)
    {
        Convert::locstruc point = base;
        point.pos.lvlh.s = offset;
        point.pos.lvlh.v = rv_zero();
        point.pos.lvlh.a = rv_zero();
        base.pos.lvlh = point.pos.lvlh;
        if (planec) { Convert::pos_lvlh2planec(base, point); }
        else if (moon) { Convert::pos_lvlh2selc(base, point); }
        else { Convert::pos_lvlh2geoc(base, point); }
        double dist = length_rv(rv_sub(moon ? point.pos.selc.s : point.pos.geoc.s, moon ? base.pos.selc.s : base.pos.geoc.s));
        base.pos.lvlh.s = rv_zero();
        if (planec) { Convert::pos_planec2lvlh(point, base); }
        else if (moon) { Convert::pos_selc2lvlh(point, base); }
        else { Convert::pos_geoc2lvlh(point, base); }
        double err = length_rv(rv_sub(base.pos.lvlh.s, offset));
        bool ok = fabs(dist - length_rv(offset)) < 1e-3 && err < 1e-3;
        printf("%s %s: separation %.4f m, offset error %.6f m\n", ok ? "PASS" : "FAIL", name, dist, err);
        if (!ok) { ++failures; }
    };

    Convert::locstruc earth = cinfo->node.loc;
    Convert::pos_set_geoc(earth);
    roundtrip("geoc", earth, false, false);

    Convert::locstruc moon;
    moon.pos.selc.utc = currentmjd();
    moon.pos.selc.s = rvector(RMOONM + 100000., 0., 0.);
    moon.pos.selc.v = rvector(0., 0., 1633.);
    Convert::pos_set_selc(moon);
    printf("%s lunar base closest is the Moon\n", moon.pos.extra.closest == COSMOS_MOON ? "PASS" : "FAIL");
    if (moon.pos.extra.closest != COSMOS_MOON) { ++failures; }
    roundtrip("selc", moon, true, false);
    roundtrip("planec at the Moon", moon, true, true);
    roundtrip("planec at the Earth", earth, false, true);

    return failures;
}
