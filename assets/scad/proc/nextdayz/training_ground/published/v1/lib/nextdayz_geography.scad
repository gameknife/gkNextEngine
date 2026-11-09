// NextDayz geographic scene gameplay. Geometry and semantic markers stay separate.
use <kit_abandoned.scad>
use <kit_coldwar.scad>
module nd_spawn_player_safe() { color([0.1,0.8,0.2]) sphere(r=0.18,$fn=6); }
module nd_loot(p="residential") { color([0.2,0.4,0.9]) sphere(r=0.12,$fn=6); }
module nd_zed(p="civilian",group=1) { color([0.8,0.2,0.2]) sphere(r=0.16,$fn=6); }
module nd_poi(name="",kind="village") { color([0.9,0.85,0.2]) sphere(r=0.2,$fn=6); }
module nd_extraction(id="",name="Helipad") { color([0,0.9,0.9]) sphere(r=0.15,$fn=6); }

module nd_geo_vegetation(kind="tree",species="birch",seed=0)
{
    gk_flatten()
    {
        if(species=="pine") cw_nature_pine(seed=seed);
        else if(species=="dead") cw_nature_tree_dead(seed=seed);
        else if(species=="bush") cw_nature_bush(seed=seed);
        else if(species=="thicket") az_nature_thicket(seed=seed);
        else cw_nature_birch(seed=seed);
    }
}

module nd_geo_building(id=0,region="bohemia",seed=0,L=10,D=8,kind="house",p="residential")
{
    gk_flatten()
    {
        if(kind=="castle") az_bldg_castle_keep(seed,L,D);
        else if(region=="hokkaido" && kind=="warehouse") az_bldg_otaru_warehouse(seed,L,D);
        else if(region=="hokkaido") az_bldg_otaru_rowhouse(seed,L,D);
        else if(kind=="cottage") az_bldg_loket_cottage(seed,L,D);
        else if(kind=="shop") az_bldg_loket_shop(seed,L,D);
        else if(kind=="workshop") az_bldg_loket_workshop(seed,L,D);
        else if(kind=="villa") az_bldg_loket_villa(seed,L,D);
        else if(kind=="ruin") az_bldg_loket_ruin(seed,L,D);
        else az_bldg_loket_townhouse(seed,L,D,1+az_rnd(seed,3));
        // Fixed room scale: buildings are fitted by parameters, never by scaling doors.
        translate([-L*0.27,D*0.28,0.15]) cw_prop_table(seed,1.5,0.8);
        translate([L*0.27,D*0.29,0.15]) cw_prop_shelf_wood(seed,1.6);
        translate([-L*0.28,-D*0.18,0.15]) cw_prop_chair(seed);
        translate([L*0.28,-D*0.19,0.15]) cw_prop_trash(seed);
        if(kind=="warehouse")
        {
            translate([-L*0.3,0,0.15]) cw_prop_workbench(seed);
            translate([L*0.3,0,0.15]) cw_prop_barrel(seed);
        }
        else translate([-L*0.23,D*0.1,0.15]) cw_prop_bed(seed);
    }
    translate([-L*0.27,D*0.28,0.15+(cw_rnd(seed,4)==0 ? 0.02 : 0.77)]) nd_loot(p=p);
    translate([L*0.27,D*0.29,0.82]) nd_loot(p=p);
    translate([0,-D*0.15,0.14]) nd_loot(p=p);
}

module nd_geo_extraction(id="",name="Helipad",seed=0)
{
    gk_flatten() az_prop_helipad(seed);
    translate([0,0,0.15]) nd_extraction(id=id,name=name);
}
