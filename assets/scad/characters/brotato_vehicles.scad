// ============================================================================
// brotato_vehicles.scad - Brotato3D vehicles, packed as a ScadRig catalog of
// static models (the trick nextdayz_weapons.scad uses for weapons).
//
// bone_veh_bus is the survivors' bus that pulls up at dusk. kit_deadly vehicles
// face +X; the catalog turns it to face -Y so the model's front is engine +Z
// and Brotato3D can yaw it straight into its direction of travel. The door is
// on the bus's right (engine -X) toward the front.
// ============================================================================

use <../lib/kit_deadly.scad>
use <../lib/kit_deadly_wasteland.scad>

$fn = 12;

module bone_veh_bus() { rotate([0, 0, -90]) dw_veh_survivor_bus(); }

module bone_root()
{
    bone_veh_bus();
}

bone_root();
