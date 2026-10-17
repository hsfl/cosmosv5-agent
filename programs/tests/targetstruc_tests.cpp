#include "support/configCosmos.h"
#include "physics/simulatorclass.h"
#include "support/datalib.h"
#include "support/jsonlib.h"
#include <cstdio>
#include <fstream>

// Exercises the target and realm "shortcuts" (jsonlib.h), realm node discovery
// and ground-station-as-node target synthesis against a throwaway realm:
//   <tmp>/testrealm/targets.json          2 permanent targets
//   <tmp>/testrealm/nodes/gs1.json        ground station  -> becomes a target
//   <tmp>/testrealm/nodes/moc1.json       MOC             -> never a target
//   <tmp>/testrealm/nodes/sat1.json       satellite
// Returns the number of failed checks.

static int failures = 0;

static void check(bool ok, const string &what)
{
    cout << (ok ? "PASS " : "FAIL ") << what << endl;
    if (!ok) { ++failures; }
}

static bool near(double a, double b, double tol=1e-9)
{
    return fabs(a - b) <= tol;
}

static void write_file(const string &path, const string &text)
{
    std::ofstream ofs(path);
    ofs << text;
}

static string node_file(const string &name, int type)
{
    nodestruc node;
    node.name = name;
    node.type = type;
    node.loc.pos.geod.s = { RADOF(21.3), RADOF(-157.9), 10. };
    node.loc.pos.geod.pass = 1;
    return json11::Json(json11::Json::object { { "jtype", "node" }, { "node", node } }).dump();
}

int main(int argc, char *argv[])
{
    // Throwaway realm tree
    char tmpl[] = "/tmp/targetstruc_tests_XXXXXX";
    string root = mkdtemp(tmpl);
    string realm = "testrealm";
    string realmdir = root + "/" + realm;
    COSMOS_MKDIR(realmdir.c_str(), 00777);
    COSMOS_MKDIR((realmdir + "/nodes").c_str(), 00777);
    write_file(realmdir + "/targets.json",
               "[\n"
               "  {\"name\":\"Faustini\",\"type\":5,\"body\":\"moon\",\"lat\":-87.2,\"lon\":85.0,\"h\":0,\"radius\":2100},\n"
               "  {\"name\":\"Ewa\",\"type\":5,\"lat\":21.3,\"lon\":-158.0,\"h\":5,\"area\":1e6,\"minel\":10}\n"
               "]\n");
    write_file(realmdir + "/nodes/gs1.json", node_file("gs1", NODE_TYPE_GROUNDSTATION));
    write_file(realmdir + "/nodes/moc1.json", node_file("moc1", NODE_TYPE_MOC));
    write_file(realmdir + "/nodes/sat1.json", node_file("sat1", NODE_TYPE_SATELLITE));
    set_cosmosrealms(root, false);

    // Target shortcut round trip
    targetstruc moon;
    string error;
    json11::Json payload = json11::Json::parse("{\"name\":\"Faustini\",\"type\":5,\"body\":\"moon\",\"lat\":-87.2,\"lon\":85.0,\"h\":0,\"radius\":2100}", error);
    check(json_target_payload_load(moon, payload) == 0, "target payload loads");
    check(moon.loc.pos.extra.closest == COSMOS_MOON, "moon target is on the Moon");
    check(near(moon.loc.pos.selg.s.lat, RADOF(-87.2)), "moon target latitude (selg)");
    check(near(moon.size.lat, 2100. / RMOONM), "moon target size uses lunar radius");
    json11::Json out = json_target_payload(moon);
    check(out["jtype"].string_value() == "target", "target payload is tagged jtype:target");
    targetstruc back;
    check(json_target_payload_load(back, out) == 0, "target payload reloads its own output");
    check(back.name == "Faustini" && back.type == 5, "target name/type round trip");
    check(near(back.loc.pos.selg.s.lat, moon.loc.pos.selg.s.lat) && near(back.loc.pos.selg.s.lon, moon.loc.pos.selg.s.lon), "target position round trip");
    check(near(back.area, moon.area, 1e-3), "target area round trip");

    // Realm node discovery
    vector<nodestruc> nodes;
    check(json_load_realm_nodes(realm, nodes) == 3, "json_load_realm_nodes finds 3 nodes");
    vector<nodestruc> stations;
    check(json_load_realm_groundstations(realm, stations) == 1 && stations[0].name == "gs1", "only the ground station is a station (MOC excluded)");

    // Realm load: targets.json
    cosmosstruc *cinfo = json_init(realm);
    check(json_setup_realm(realm, cinfo) == 2, "json_setup_realm loads 2 targets");
    check(cinfo->target.size() == 2 && cinfo->target[1].name == "Ewa" && near(cinfo->target[1].min, RADOF(10.), 1e-6), "earth target minel");

    // Simulator: targets.json plus ground station nodes
    Physics::Simulator sim;
    sim.realmname = realm;
    Convert::tlestruc tle;
    sim.AddNode("sat1", "U12", Physics::Propagator::PositionTle, Physics::Propagator::AttitudeLVLH, Physics::Propagator::Thermal, Physics::Propagator::Electrical, tle);
    check(sim.cnodes.size() == 1, "simulator has one node");
    sim.ParseTargetFile(realmdir + "/targets.json");
    check(sim.AddGroundStations() == 1, "AddGroundStations adds one station");
    vector<targetstruc> &simtargets = sim.cnodes[0]->currentinfo.target;
    size_t gscount = 0;
    for (targetstruc &t : simtargets)
    {
        if (t.type == NODE_TYPE_GROUNDSTATION) { ++gscount; check(t.name == "gs1", "station target is gs1"); }
        check(t.name != "moc1", "MOC is not a target: " + t.name);
    }
    check(simtargets.size() == 3 && gscount == 1, "node sees 2 targets + 1 station");

    // Realm shortcut: station targets are left out, and kept on load
    cinfo->target = simtargets;
    json11::Json rp = json_realm_payload(cinfo);
    check(rp["jtype"].string_value() == "realm" && rp["realm"]["name"].string_value() == realm, "realm payload tagged and named");
    check(rp["targets"].array_items().size() == 2, "realm payload leaves out station targets");
    cosmosstruc *cinfo2 = json_init(realm);
    cinfo2->target.push_back(target_from_node(stations[0]));
    check(json_realm_payload_load(cinfo2, rp.dump()) == 0, "realm payload loads");
    check(cinfo2->realm.name == realm && cinfo2->target.size() == 3, "realm payload restores targets and keeps the station");

    json_destroy(cinfo);
    json_destroy(cinfo2);

    for (string f : { "targets.json", "nodes/gs1.json", "nodes/moc1.json", "nodes/sat1.json" })
    {
        remove((realmdir + "/" + f).c_str());
    }
    rmdir((realmdir + "/nodes").c_str());
    rmdir(realmdir.c_str());
    rmdir(root.c_str());

    cout << (failures ? "FAILED: " + std::to_string(failures) : string("all passed")) << endl;
    return failures;
}
