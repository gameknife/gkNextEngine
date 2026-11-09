// Tactical level PCG. Geography supplies terrain/routes/district reference only.
// Site=[id,name,kind,center,padSize,rotation,buildingCount,risk].
use <kit_layout.scad>
use <kit_terrain.scad>
use <kit_coldwar.scad>
use <kit_abandoned.scad>
use <kit_bohemia.scad>
use <kit_hokkaido.scad>
use <nextdayz_coldwar.scad>
use <nextdayz_settlement.scad>
use <nextdayz_training.scad>

function nd_pcg_pads(sites,spawns,exits) = concat(
    [for(p=sites) if(!nd_is_settlement(p[2])) ["pad",p[3],p[4],p[5]]],
    [for(p=spawns) ["pad",p,[14,14],0]],
    [for(p=exits) ["pad",p,[42,42],0]]);

// Full gameplay TERR layer: land-cover paints, legacy encounter pads, settlement
// plots / streets / ponds, then spawn and extraction pads. seed must match the
// one nd_pcg_site receives (recipe seed; site i uses seed+i*131).
function nd_pcg_terrain(sites,spawns,exits,seed,landcover=[]) = concat(
    nd_pcg_landcover_paint(landcover),
    [for(p=sites) if(!nd_is_settlement(p[2]) && !nd_is_training(p[2])) ["pad",p[3],p[4],p[5]]],
    nd_settlement_terrain(sites,seed),
    nd_tg_terrain(sites),
    [for(p=spawns) ["pad",p,[14,14],0]],
    [for(p=exits) ["pad",p,[42,42],0]]);

module nd_pcg_bridge(t,a,b,width=4,seed=0)
{
    za=gk_terrain_height(t,a[0],a[1])+0.06;
    zb=gk_terrain_height(t,b[0],b[1])+0.06;
    translate([(a[0]+b[0])/2,(a[1]+b[1])/2,(za+zb)/2]) rotate([0,0,atan2(b[1]-a[1],b[0]-a[0])])
        gk_flatten() az_prop_bridge(seed,norm(b-a)+3,width,zb-za,max(10,(za+zb)/2-10+8));
}

function nd_pcg_distance(p,a,b) = let(d=b-a,t=max(0,min(1,((p[0]-a[0])*d[0]+(p[1]-a[1])*d[1])/max(0.01,d[0]*d[0]+d[1]*d[1])))) norm(p-(a+t*d));
function nd_pcg_outside_sites(x,y,sites,r,i=0) = i>=len(sites) ? true :
    let(p=sites[i],dx=x-p[3][0],dy=y-p[3][1],a=p[5],u=dx*cos(a)+dy*sin(a),v=-dx*sin(a)+dy*cos(a))
    (abs(u)>p[4][0]/2+r || abs(v)>p[4][1]/2+r) && nd_pcg_outside_sites(x,y,sites,r,i+1);
function nd_pcg_outside_circles(x,y,points,r,i=0) = i>=len(points) ? true :
    norm([x-points[i][0],y-points[i][1]])>r && nd_pcg_outside_circles(x,y,points,r,i+1);
function nd_pcg_outside_corridors(x,y,paths,r,i=0) = i>=len(paths) ? true :
    nd_pcg_distance([x,y],paths[i][0],paths[i][1])>paths[i][2]/2+r && nd_pcg_outside_corridors(x,y,paths,r,i+1);
function nd_pcg_clear(t,x,y,sites,spawns,exits,paths,r) =
    nd_pcg_outside_sites(x,y,sites,r) && nd_pcg_outside_circles(x,y,spawns,6+r) &&
    nd_pcg_outside_circles(x,y,exits,18+r) && nd_pcg_outside_corridors(x,y,paths,r);

// Settlement kinds have regional shells. Hokkaido is dispatched before the
// legacy fallback so every port, medical and military shell stays regional.
function nd_kind_profile(kind) =
    kind=="barn" || kind=="stable" || kind=="cowshed" ? "farm" :
    kind=="shop" || kind=="pub" ? "commercial" : kind=="pharmacy" ? "medical" :
    kind=="townhall" || kind=="office" ? "police" : kind=="firestation" || kind=="factory" || kind=="warehouse" || kind=="workshop" ? "industrial" :
    kind=="chata" || kind=="shrine" ? "wilderness" : kind=="castle" ? "military" : "residential";
function nd_kind_fallback(kind) =
    kind=="farmhouse" || kind=="chata" ? "cottage" : kind=="egerland" || kind=="burgher" || kind=="kostka" ? "townhouse" :
    kind=="shop" || kind=="pharmacy" || kind=="pub" || kind=="townhall" ? "shop" :
    kind=="stable" ? "workshop" : kind=="cowshed" || kind=="factory" ? "warehouse" :
    kind=="firestation" ? "garage" : kind=="church" || kind=="chapel" ? "shop" : kind;
function nd_opt(o,i,d) = len(o)>i ? o[i] : d;

// Coldwar owns furniture + typed loot positions. Only the regional shell changes.
module nd_pcg_building(kind="cottage",seed=0,region="bohemia",L=10,D=8,p="residential",opt=[])
{
    if(region=="training") nd_tg_building(kind,seed,L,D);
    else if(region=="hokkaido") nd_hk_building(kind,seed,L,D,p);
    else if(region=="bohemia" && (kind=="farmhouse" || kind=="cottage"))
        nd_house_rural(seed,L,D,p) bh_bldg_farmhouse(seed,L,D,nd_opt(opt,0,1));
    else if(region=="bohemia" && kind=="egerland") nd_house_two(seed,L,D,p) bh_bldg_egerland(seed,L,D,nd_opt(opt,0,1));
    else if(region=="bohemia" && kind=="barn") nd_barn(seed,L,D,"farm") bh_bldg_barn(seed,L,D);
    else if(region=="bohemia" && (kind=="stable" || kind=="workshop")) nd_barn(seed,L,D,"farm") bh_bldg_stable(seed,L,D);
    else if(region=="bohemia" && (kind=="burgher" || kind=="townhouse"))
        nd_house_two(seed,L,D,p) bh_bldg_burgher(seed,L,D,nd_opt(opt,0,2),nd_opt(opt,1,-1),false);
    else if(region=="bohemia" && kind=="shop") nd_shop(seed,L,D,"commercial") bh_bldg_burgher(seed,L,D,nd_opt(opt,0,2),nd_opt(opt,1,-1),true);
    else if(region=="bohemia" && kind=="pharmacy") nd_shop(seed,L,D,"medical") bh_bldg_burgher(seed,L,D,nd_opt(opt,0,2),nd_opt(opt,1,-1),true);
    else if(region=="bohemia" && kind=="pub") nd_pub(seed,L,D) bh_bldg_burgher(seed,L,D,2,0,false);
    else if(region=="bohemia" && kind=="townhall") nd_office(seed,L,D,"police") bh_bldg_townhall(seed,L,D);
    else if(region=="bohemia" && kind=="church") nd_church(seed,L,D) bh_bldg_church(seed,L,D);
    else if(region=="bohemia" && kind=="chapel") nd_chapel(seed,L,D) bh_bldg_chapel(seed,L,D);
    else if(region=="bohemia" && kind=="firestation") nd_depot(seed,L,D,"industrial") bh_bldg_firestation(seed,L,D);
    else if(region=="bohemia" && kind=="villa") nd_house_two(seed,L,D,p) bh_bldg_villa(seed,L,D);
    else if(region=="bohemia" && kind=="kostka") nd_house_two(seed,L,D,p) bh_bldg_kostka(seed,L,D);
    else if(region=="bohemia" && kind=="chata") nd_house_rural(seed,L,D,"wilderness") bh_bldg_chata(seed,L,D);
    else if(region=="bohemia" && (kind=="cowshed" || kind=="warehouse")) nd_barn(seed,L,D,kind=="cowshed" ? "farm" : "industrial") bh_bldg_cowshed(seed,L,D);
    else if(region=="bohemia" && kind=="factory") nd_warehouse(seed,L,D,"industrial") bh_bldg_factory(seed,L,D);
    else if(region=="bohemia" && kind=="castle") nd_castle_hall(seed,L,D) bh_bldg_castle(seed,L,D);
    else if(nd_kind_fallback(kind)!=kind) nd_pcg_building(nd_kind_fallback(kind),seed,region,L,D,p);
    else if(kind=="cottage")
        nd_house_rural(seed,L,D,p)
            if(region=="sichuan") az_bldg_sichuan_courtyard(seed,L,D);
            else if(region=="hokkaido") az_bldg_otaru_timber(seed,L,D);
            else az_bldg_loket_cottage(seed,L,D);
    else if(kind=="townhouse" || kind=="villa" || kind=="ruin")
        nd_house_two(seed,L,D,p)
            if(region=="sichuan" && kind=="ruin") az_bldg_sichuan_ruin(seed,L,D);
            else if(region=="sichuan" && kind=="villa") az_bldg_sichuan_courtyard(seed,L,D);
            else if(region=="sichuan") az_bldg_sichuan_rowhouse(seed,L,D);
            else if(region=="hokkaido" && kind=="ruin") az_bldg_otaru_ruin(seed,L,D);
            else if(region=="hokkaido") az_bldg_otaru_rowhouse(seed,L,D);
            else if(kind=="villa") az_bldg_loket_villa(seed,L,D);
            else if(kind=="ruin") az_bldg_loket_ruin(seed,L,D);
            else az_bldg_loket_townhouse(seed,L,D,2);
    else if(kind=="shop")
        nd_shop_row(seed,L,D)
            if(region=="sichuan") az_bldg_sichuan_teahouse(seed,L,D);
            else if(region=="hokkaido") az_bldg_otaru_shophouse(seed,L,D);
            else az_bldg_loket_shop(seed,L,D);
    else if(kind=="workshop" || kind=="warehouse")
        nd_warehouse(seed,L,D,p)
            if(region=="sichuan") az_bldg_sichuan_service(seed,L,D);
            else if(region=="hokkaido" && kind=="workshop") az_bldg_otaru_workshop(seed,L,D);
            else if(region=="hokkaido") az_bldg_otaru_warehouse(seed,L,D);
            else az_bldg_loket_workshop(seed,L,D);
    else if(kind=="apartment") nd_panel_flat(seed,3,L,D)
        if(region=="sichuan") az_bldg_sichuan_rowhouse(seed,L,D); else az_bldg_otaru_apartment(seed,L,D);
    else if(kind=="visitor") nd_shop_row(seed,L,D) az_bldg_qinglong_visitor(seed,L,D);
    else if(kind=="pavilion")
    {
        gk_flatten()
        {
            az_bldg_sichuan_pavilion(seed,L,D);
            translate([-L*0.28,D*0.2,0.12]) cw_prop_bench();
            translate([L*0.25,D*0.18,0.12]) cw_prop_table(seed,1.5,0.8);
        }
        translate([-L*0.28,D*0.2,0.55]) nd_loot(p="commercial");
        translate([L*0.25,D*0.18,0.12+nd_table_top(seed)]) nd_loot(p="commercial");
        translate([0,-D*0.25,0.12]) nd_loot(p="wilderness");
    }
    else if(kind=="medical") nd_hospital(seed,2,L,D);
    else if(kind=="police") nd_police(seed);
    else if(kind=="barracks") nd_barracks(seed,L,D);
    else if(kind=="hq") nd_hq(seed,L,D);
    else if(kind=="barn")
        if(region=="sichuan") nd_barn(seed,L,D) az_bldg_sichuan_service(seed,L,D);
        else nd_barn(seed,L,D);
    else if(kind=="garage") nd_garage(seed);
    else if(kind=="supermarket") nd_supermarket(seed,L,D);
    else if(kind=="castle")
    {
        gk_flatten() { az_bldg_castle_keep(seed,L,D); translate([0,2,0]) cw_prop_crate_ammo(seed); }
        translate([0,2,0.4]) nd_loot(p="military");
        translate([-L*0.25,0,0.12]) nd_loot(p="medical");
        translate([L*0.25,0,0.12]) nd_loot(p="military");
    }
}

module nd_hk_building(kind="cottage",seed=0,L=10,D=8,p="residential")
{
    if(kind=="warehouse") nd_warehouse(seed,L,D,p) hk_bldg_warehouse(seed,L,D);
    else if(kind=="workshop" || kind=="garage") nd_depot(seed,L,D,"industrial") hk_bldg_workshop(seed,L,D);
    else if(kind=="office" || kind=="hq") nd_office(seed,L,D,kind=="hq" ? "military" : "police") hk_bldg_office(seed,L,D);
    else if(kind=="medical") nd_office(seed,L,D,"medical") hk_bldg_clinic(seed,L,D);
    else if(kind=="police") nd_office(seed,L,D,"police") hk_bldg_koban(seed,L,D);
    else if(kind=="shop" || kind=="pharmacy" || kind=="supermarket")
        nd_shop(seed,L,D,kind=="pharmacy" ? "medical" : "commercial") hk_bldg_shop(seed,L,D);
    else if(kind=="pub") nd_pub(seed,L,D) hk_bldg_shop(seed,L,D);
    else if(kind=="shrine") nd_chapel(seed,L,D) hk_bldg_shrine(seed,L,D);
    else if(kind=="apartment") nd_house_two(seed,L,D,p) hk_bldg_apartment(seed,L,D);
    else if(kind=="townhouse") nd_house_two(seed,L,D,p) hk_bldg_rowhouse(seed,L,D);
    else if(kind=="ruin") nd_house_rural(seed,L,D,p) hk_bldg_ruin(seed,L,D);
    else if(kind=="barracks") nd_house_rural(seed,L,D,"military") hk_bldg_workshop(seed,L,D);
    else nd_house_rural(seed,L,D,p) hk_bldg_house(seed,L,D);
}

function nd_pcg_kind(type,i,seed) =
    type=="park" ? (i==0 ? "pavilion" : i==1 ? "shop" : i%3==0 ? "ruin" : "cottage") :
    type=="visitor" ? (i==0 ? "visitor" : i==1 ? "shop" : i==2 ? "workshop" : "cottage") :
    type=="urban" ? (i%8==2 ? "apartment" : ["townhouse","shop","cottage","ruin","townhouse","cottage"][az_rnd(seed+i*71,6)]) :
    type=="downtown" ? (i%7==2 ? "apartment" : i%9==3 ? "supermarket" : ["shop","workshop","shop","townhouse"][az_rnd(seed+i*71,4)]) :
    type=="port" ? ["warehouse","warehouse","workshop","garage"][i%4] :
    type=="market" ? (i==0 ? "supermarket" : (i%3==0 ? "townhouse" : "shop")) :
    type=="medical" ? (i==0 ? "medical" : (i==1 ? "townhouse" : "garage")) :
    type=="industry" ? (i%3==0 ? "garage" : (i%2==0 ? "workshop" : "warehouse")) :
    type=="military" ? (i==0 ? "hq" : (i%2==0 ? "warehouse" : "barracks")) :
    type=="checkpoint" ? (i==0 ? "police" : (i%2==0 ? "garage" : "barracks")) :
    type=="farm" ? (i%3==0 ? "barn" : (i%2==0 ? "cottage" : "workshop")) :
    ["cottage","townhouse","shop","villa","ruin","cottage"][(i+az_rnd(seed,6))%6];

module nd_pcg_site(t,site,region="bohemia",seed=0)
{
    // Legacy encounters share one large pad and one datum, as in Riverland.
    // Settlements are terraced per plot: every item stands on its own ground.
    translate([site[3][0],site[3][1],gk_terrain_height(t,site[3][0],site[3][1])]) rotate([0,0,site[5]])
        nd_pcg_encounter(site[0],site[1],site[2],site[4],site[6],site[7],region,seed,t,site[3],site[5]);
}

function nd_poi_kind(type) = type=="medical" ? "hospital" : type=="military" || nd_is_training(type) ? "military" :
    type=="hillcastle" || type=="castle" ? "fortress" : type=="square" || type=="street" ? "town" :
    type=="jzd" || type=="factory" || type=="industry" ? "industrial" : type=="homestead" || type=="farm" ? "farm" : "village";

module nd_pcg_encounter(id="site",name="Site",type="village",pad=[76,60],count=6,risk=1,region="bohemia",seed=0,t=[],at=[0,0],angle=0)
{
    nd_poi(name=name,kind=nd_poi_kind(type));
    if(nd_is_settlement(type))
    {
        items=nd_settlement([id,name,type,at,pad,angle,count,risk],seed);
        z0=gk_terrain_height(t,at[0],at[1]);
        for(i=[0:len(items)-1])
        {
            it=items[i];
            if(it[0]=="bldg" || it[0]=="prop" || it[0]=="plant" || it[0]=="loot" || it[0]=="zed")
            {
                w=nd_xf([it[1],it[2]],at,angle);
                translate([it[1],it[2],gk_terrain_height(t,w[0],w[1])-z0]) rotate([0,0,it[3]])
                    nd_pcg_item(it,region,seed+i*17);
            }
        }
    }
    else if(nd_is_training(type)) nd_tg_site(id,type,pad,count,seed);
    else nd_pcg_legacy(id,name,type,pad,count,risk,region,seed);
}

module nd_pcg_item(it=[],region="bohemia",seed=0)
{
    if(it[0]=="bldg") nd_pcg_building(it[4],seed,region,it[5],it[6],nd_kind_profile(it[4]),it[7]);
    else if(it[0]=="prop") nd_pcg_prop(it[4],seed,it[5],it[6]);
    else if(it[0]=="plant") scale([it[5],it[5],it[5]]) nd_site_plant(it[4],seed%4,region);
    else if(it[0]=="loot") translate([0,0,0.12]) nd_loot(p=it[4]);
    else if(it[0]=="zed") translate([0,0,0.12]) nd_zed(p=it[4],group=it[5]);
}

// Settlement furniture. The water pump stays outside gk_flatten: its node name
// registers a well anchor.
module nd_pcg_prop(name="bench",seed=0,a=0,b=0)
{
    if(name=="pump") cw_prop_water_pump(seed);
    else gk_flatten()
    {
        if(name=="hk_torii") hk_prop_torii(seed,a,b);
        else if(name=="hk_pole") hk_prop_pole(seed);
        else if(name=="hk_bollard") hk_prop_bollard(seed);
        else if(name=="hk_vending") hk_prop_vending(seed);
        else if(name=="hk_lantern") hk_prop_lantern(seed);
        else if(name=="yard_wall") bh_prop_yard_wall(seed,a,2.5,b);
        else if(name=="fence") bh_prop_fence(seed,a);
        else if(name=="plague_column") bh_prop_plague_column(seed);
        else if(name=="fountain") bh_prop_fountain(seed);
        else if(name=="memorial") bh_prop_memorial(seed);
        else if(name=="cross") bh_prop_cross(seed);
        else if(name=="shrine") bh_prop_shrine(seed);
        else if(name=="bus_shelter") bh_prop_bus_shelter(seed);
        else if(name=="pole") bh_prop_pole(seed);
        else if(name=="bale") bh_prop_bale(seed);
        else if(name=="silo") bh_prop_silo(seed);
        else if(name=="chimney") bh_prop_chimney(seed);
        else if(name=="woodpile") cw_prop_woodpile(seed);
        else if(name=="car") cw_veh_car_burnt(seed);
        else if(name=="tractor") cw_veh_tractor(seed);
        else if(name=="truck") cw_veh_truck_canvas(seed);
        else if(name=="fueltank") cw_prop_fueltank(seed);
        else if(name=="fence_concrete") cw_prop_fence_concrete(len=a);
        else if(name=="lamp") cw_prop_lamp();
        else if(name=="bench") cw_prop_bench();
    }
}

module nd_site_plant(species="linden",variant=0,region="bohemia")
{
    gk_flatten()
        if(region=="hokkaido") nd_hk_flora(species,variant*131+7);
        else if(species=="linden") bh_nature_linden(variant*131+7);
        else if(species=="fruit") bh_nature_fruit(variant*131+11);
        else if(species=="willow") bh_nature_willow(variant*131+13);
        else if(species=="birch") bh_nature_birch(variant*131+17);
        else if(species=="pine") bh_nature_pine(variant*131+19);
        else if(species=="spruce_garden") bh_nature_spruce(variant*131+23,9);
        else bh_nature_spruce(variant*131+29);
}

module nd_pcg_legacy(id="site",name="Site",type="village",pad=[76,60],count=6,risk=1,region="bohemia",seed=0)
{
    city=type=="urban" || type=="downtown" || type=="port";
    rows=city ? 4 : 2;
    cols=ceil(count/rows);
    pitch=(pad[0]-(city ? 28 : 20))/max(1,cols);
    rowPitch=(pad[1]-32)/4;
    profile=type=="military" || type=="checkpoint" || type=="castle" ? "military" :
        type=="industry" || type=="port" ? "industrial" : type=="downtown" || type=="visitor" ? "commercial" : type=="farm" ? "farm" : type=="park" ? "wilderness" : "residential";
    if(type=="castle")
    {
        translate([0,10,0]) nd_pcg_building("castle",seed,region,26,20);
        for(i=[1:count-1])
            translate([(i-2)*22,-18,0]) rotate([0,0,180])
                nd_pcg_building(i==1 ? "barracks" : i==2 ? "workshop" : "hq",seed+i*17,region,12,8,"military");
    }
    else for(i=[0:count-1])
    {
        kind=nd_pcg_kind(type,i,seed);
        row=floor(i/cols);
        L=kind=="medical" ? 18 : kind=="supermarket" ? (city ? 18 : 16) : kind=="apartment" ? min(20,pitch-5) :
            city ? min(kind=="warehouse" ? 26 : kind=="workshop" ? 20 : kind=="shop" ? 16 : 14,pitch-5) : min(12,pitch-3);
        D=kind=="apartment" ? 11 : city ? (kind=="warehouse" ? 14 : kind=="workshop" ? 12 : 10) : kind=="medical" || kind=="supermarket" ? 9 : 8;
        translate([(i%cols-(cols-1)/2)*pitch,city ? (1.5-row)*rowPitch : (i<cols ? 1 : -1)*min(pad[1]*0.24,pad[1]/2-13),0])
            rotate([0,0,row%2==0 ? 0 : 180]) nd_pcg_building(kind,seed+i*17,region,L,D,profile);
    }
    // Cover leaves both flanks of the central lane open. Risk and rewards belong
    // to the encounter template, rather than to the number of mapped OSM plots.
    gk_flatten()
    {
        translate([5,0,0]) rotate([0,0,12]) cw_veh_car_burnt(seed);
        translate([-9,-2,0]) cw_prop_wall_sandbag(len=4);
        translate([pad[0]*0.28,2,0]) cw_prop_barrel(seed+3);
        translate([-pad[0]*0.28,-2,0]) cw_prop_crate_ammo(seed+7);
        translate([0,-pad[1]*0.35,0]) cw_prop_trash(seed+11);
        if(risk>=3) translate([pad[0]*0.32,-pad[1]*0.32,0]) cw_prop_searchlight(seed);
        if(type=="village" || type=="farm") translate([pad[0]*0.32,0,0]) cw_prop_water_pump(seed);
        if(city)
        {
            // Two through streets and a rear alley; cover never seals their ends.
            for(sy=[-1,1],sx=[-1,1])
                translate([sx*(pad[0]/2-10),sy*rowPitch,0]) cw_prop_lamp();
            for(sy=[-1,1],k=[0:cols-2])
                if(k%2==0)
                {
                    // Park cover between door axes, along the outer curb.
                    x=(k+0.5-(cols-1)/2)*pitch;
                    translate([x,sy*(rowPitch+5),0]) rotate([0,0,sy*6])
                        if(type=="port" && k==0) cw_veh_truck_canvas(seed+sy+23);
                        else cw_veh_car_burnt(seed+sy*31+k+23);
                    translate([x+4,sy*(rowPitch+5),0]) cw_prop_tires(seed+k);
                }
            for(sy=[-1,1])
            {
                translate([-pad[0]*0.24,sy*rowPitch,0]) cw_prop_trash(seed+sy+43);
            }
            if(type=="urban") translate([-pad[0]*0.35,0,0]) cw_prop_water_pump(seed);
            if(type=="port") for(sx=[-1,1]) translate([sx*(pad[0]/2-7),0,0]) cw_prop_fence_chain(len=10);
        }
    }
    translate([-pad[0]*0.28,-2,0.4]) nd_loot(p=profile);
    for(i=[0:risk])
        translate([(i%2==0 ? -1 : 1)*pad[0]*0.31,i<2 ? -3 : 3,0.12])
            nd_zed(p=risk>=3 ? "military" : type=="industry" ? "industrial" : "civilian",group=1+risk);
    if(city) for(sy=[-1,1]) translate([-pad[0]*0.25,sy*rowPitch,0.12]) nd_zed(p=type=="port" ? "industrial" : "civilian",group=risk+1);
}

module nd_pcg_plant(kind="tree",variant=0,seed=0,region="bohemia",species="")
{
    gk_flatten()
        if(region=="bohemia" || region=="training")
            nd_bh_flora(species!="" ? species : kind=="dead" ? "spruce_dead" : kind=="shrub" ? ["hedge","weeds","fern"][variant%3] :
                        ["spruce","broadleaf","pine","spruce"][variant%4],seed);
        else if(region=="hokkaido") nd_hk_flora(species!="" ? species : kind=="shrub" ? "shrub" : kind=="dead" ? "dead" : "birch",seed);
        else if(kind=="dead") cw_nature_tree_dead(seed=seed);
        else if(kind=="shrub") lay_pick(i=variant) { cw_nature_bush(seed=seed); cw_nature_grass_tuft(seed=seed); cw_nature_stump(seed=seed); }
        else if(kind=="bamboo") az_nature_bamboo(seed);
        else if(region=="sichuan") lay_pick(i=variant) { az_nature_camphor(seed); az_nature_willow(seed); az_nature_camphor(seed+1); }
        else lay_pick(i=variant) { cw_nature_pine(seed=seed); cw_nature_birch(seed=seed); cw_nature_pine(seed=seed+1); }
}

module nd_hk_flora(sp="birch",seed=0)
{
    if(sp=="spruce" || sp=="pine") hk_nature_spruce(seed);
    else if(sp=="birch") hk_nature_birch(seed);
    else if(sp=="oak") hk_nature_oak(seed);
    else if(sp=="elm" || sp=="broadleaf" || sp=="linden" || sp=="fruit") hk_nature_elm(seed);
    else if(sp=="alder" || sp=="willow") hk_nature_alder(seed);
    else if(sp=="dead" || sp=="spruce_dead") cw_nature_tree_dead(seed=seed);
    else if(sp=="stump") cw_nature_stump(seed=seed);
    else if(sp=="reeds") cw_nature_reeds(seed=seed);
    else hk_nature_shrub(seed);
}

// West Bohemian flora by species name (one mesh per species+seed).
module nd_bh_flora(sp="spruce",seed=0)
{
    if(sp=="spruce") bh_nature_spruce(seed);
    else if(sp=="spruce_dead") bh_nature_spruce_dead(seed);
    else if(sp=="pine") bh_nature_pine(seed);
    else if(sp=="broadleaf") bh_nature_broadleaf(seed);
    else if(sp=="oak") bh_nature_broadleaf(seed,15,true);
    else if(sp=="linden") bh_nature_linden(seed);
    else if(sp=="fruit") bh_nature_fruit(seed);
    else if(sp=="alder") bh_nature_alder(seed);
    else if(sp=="willow") bh_nature_willow(seed);
    else if(sp=="birch") bh_nature_birch(seed);
    else if(sp=="hedge") bh_nature_hedge(seed);
    else if(sp=="fern") bh_nature_fern(seed);
    else if(sp=="weeds") bh_nature_weeds(seed);
    else if(sp=="bale") bh_prop_bale(seed);
    else if(sp=="stump") cw_nature_stump(seed=seed);
}

module nd_pcg_vegetation(t,layers,sites,spawns,exits,clear,seed=0,flora="bohemia")
{
    for(i=[0:len(layers)-1])
    {
        layer=layers[i];
        ter_scatter(t,seed+i*101,layer[2],layer[1],[10.5,140,layer[3],3,["grass","grass_dark","dry_grass"]],
                    variants=12,scale=layer[4])
            if(nd_pcg_clear(t,$px,$py,sites,spawns,exits,clear,layer[5]=="shrub" ? 1 : 3))
                nd_pcg_plant(layer[5],$variant,$variant_seed,flora);
    }
}

// ================= Land cover =================
// PCG_LANDCOVER = [[id,kind,[[x,y],...]],...]: closed polygons drawn over the
// real landscape. Each kind paints the ground and grows a stand on a jittered
// grid, thinned by a broad noise so forests keep glades and ragged edges.
// recipe = [paint, spacing, slopeMax, keep, edgeShrub, [[species,kind,weight,s0,s1],...]]
function nd_lc_recipe(k) =
    k=="hokkaido_mixed" ? ["grass_dark",7.5,36,0.90,0.35,[["oak","tree",4,0.85,1.2],["elm","tree",3,0.85,1.15],["birch","tree",4,0.85,1.2],["spruce","tree",1,0.9,1.1]]] :
    k=="coastal_grass" ? ["dry_grass",12,30,0.5,0,[["reeds","shrub",3,0.9,1.3],["shrub","shrub",2,0.7,1.1]]] :
    k=="spruce" ? ["grass_dark",6.9,36,0.92,0.35,[["spruce","tree",10,0.85,1.2],["spruce_dead","dead",1,0.8,1.05],["birch","tree",1,0.8,1.1]]] :
    k=="mixed" ? ["grass_dark",7.6,36,0.88,0.4,[["broadleaf","tree",4,0.8,1.2],["spruce","tree",4,0.8,1.15],["oak","tree",1,0.85,1.2],["birch","tree",2,0.8,1.1],["pine","tree",1,0.85,1.1]]] :
    k=="beech" ? ["grass_dark",8.0,38,0.88,0.3,[["broadleaf","tree",6,0.85,1.25],["oak","tree",2,0.85,1.2],["spruce","tree",1,0.8,1.1]]] :
    k=="pine" ? ["dry_grass",7.8,32,0.82,0.3,[["pine","tree",6,0.85,1.2],["birch","tree",1,0.8,1.1],["spruce","tree",1,0.8,1.1]]] :
    k=="beetle" ? ["dry_grass",7.6,36,0.8,0.2,[["spruce_dead","dead",6,0.8,1.15],["birch","tree",3,0.55,0.95],["spruce","tree",1,0.7,1.0],["fern","shrub",3,0.9,1.4],["stump","shrub",2,1.0,1.4]]] :
    k=="riparian" ? ["grass_dark",6.8,32,0.95,0.45,[["alder","tree",5,0.85,1.15],["willow","tree",3,0.85,1.2],["birch","tree",1,0.8,1.1]]] :
    k=="orchard" ? ["grass",8.5,22,0.82,0,[["fruit","tree",1,0.9,1.25]]] :
    k=="field" ? ["dry_grass",12,20,0.32,0,[["weeds","shrub",9,0.8,1.3],["bale","shrub",1,1,1],["birch","tree",1,0.5,0.8]]] :
    k=="meadow" ? ["grass",20,28,0.32,0,[["hedge","shrub",4,0.8,1.3],["bale","shrub",2,1,1],["linden","tree",1,0.8,1.1],["broadleaf","tree",1,0.8,1.1],["fruit","tree",1,0.9,1.1]]] :
    k=="clearing" ? ["dry_grass",9.5,36,0.6,0,[["fern","shrub",4,0.8,1.3],["birch","tree",2,0.6,0.9],["spruce_dead","dead",1,0.7,1.0],["stump","shrub",2,1.0,1.3]]] :
    ["",0,0,0,0,[]];
function nd_lc_valid(k) = nd_lc_recipe(k)[1]>0;
// Fields alternate crops by index: stubble rows, ploughland, green crop.
function nd_field_paint(poly,i) = let(a=atan2(poly[1][1]-poly[0][1],poly[1][0]-poly[0][0]),v=i%3)
    v==0 ? ["paint",poly,"dry_grass",["sand",26,a]] : v==1 ? ["paint",poly,"road",["dry_grass",30,a]] : ["paint",poly,"grass_dark"];
function nd_pcg_landcover_paint(lc) = len(lc)==0 ? [] : [for(i=[0:len(lc)-1]) let(l=lc[i],r=nd_lc_recipe(l[1])) if(r[0]!="")
    l[1]=="field" ? nd_field_paint(l[2],i) : ["paint",l[2],r[0]]];

function nd_bbox(p) = [min([for(q=p) q[0]]),min([for(q=p) q[1]]),max([for(q=p) q[0]]),max([for(q=p) q[1]])];
function nd_in_poly(x,y,p,i=0,c=false) = i>=len(p) ? c :
    let(a=p[i],b=p[i==0 ? len(p)-1 : i-1],x0=(b[0]-a[0])*(y-a[1])/(b[1]-a[1]+(b[1]==a[1] ? 1e-9 : 0))+a[0])
    nd_in_poly(x,y,p,i+1,((a[1]>y)!=(b[1]>y)) && x<x0 ? !c : c);
function nd_edge_dist(q,p) = min([for(i=[0:len(p)-1]) nd_pcg_distance(q,p[i],p[(i+1)%len(p)])]);
function nd_patch(x,y,s) = 0.5+0.5*sin(x*0.022+s*37)*cos(y*0.019-s*53);
function nd_pick(sp,r,i=0) = i>=len(sp)-1 || r<sp[i][2] ? i : nd_pick(sp,r-sp[i][2],i+1);
function nd_near_water(t,x,y,d) = len([for(a=[0:45:315]) if(gk_terrain_info(t,x+d*cos(a),y+d*sin(a))[2]>0) 1])>0;

// Obstacles relevant to one bounding box, so each candidate only tests what
// can actually reject it (a stand far from every site skips all site maths).
function nd_rect_near(s,bb,m) = let(e=abs(s[4][0]/2*cos(s[5]))+abs(s[4][1]/2*sin(s[5]))+m,f=abs(s[4][0]/2*sin(s[5]))+abs(s[4][1]/2*cos(s[5]))+m)
    s[3][0]+e>bb[0] && s[3][0]-e<bb[2] && s[3][1]+f>bb[1] && s[3][1]-f<bb[3];
function nd_pt_near(p,bb,m) = p[0]+m>bb[0] && p[0]-m<bb[2] && p[1]+m>bb[1] && p[1]-m<bb[3];
function nd_seg_near(c,bb,m) = nd_pt_near(c[0],[min(bb[0],bb[2])-norm(c[1]-c[0]),bb[1]-norm(c[1]-c[0]),bb[2]+norm(c[1]-c[0]),bb[3]+norm(c[1]-c[0])],m);
function nd_ground_ok(t,x,y,slopeMax) = let(info=gk_terrain_info(t,x,y)) info[2]==0 && info[1]<=slopeMax && info[3]<=2;
function nd_cell_wet(t,x,y,c,wl) = min([for(q=[[0,0],[c,0],[0,c],[c,c],[c/2,c/2]]) gk_terrain_height(t,x+q[0],y+q[1])])<wl+2.5;

// Accepted stand points [x,y,s] of one land-cover polygon (functions build no nodes).
function nd_stand_points(t,lc,i,r,poly,bb,ls,lsp,lex,lcl,seed,wl) = let(sp=r[1],
    cs=lc[1]=="riparian" ? 4*sp : (bb[2]-bb[0]+bb[3]-bb[1])+sp) [
    for(cx=[bb[0]:cs:bb[2]],cy=[bb[1]:cs:bb[3]]) if(lc[1]!="riparian" || nd_cell_wet(t,cx,cy,cs,wl))
        for(gx=[cx+sp/2:sp:min(cx+cs,bb[2])],gy=[cy+sp/2:sp:min(cy+cs,bb[3])])
            let(s=seed*31+i*7919+round(gx*13)*3+round(gy*7)*11,x=gx+(nd_u(s,1)-0.5)*sp*0.85,y=gy+(nd_u(s,2)-0.5)*sp*0.85)
            if(nd_u(s,3)<r[3]*(lc[1]=="orchard" || lc[1]=="field" ? 1 : 0.4+0.6*nd_patch(x,y,seed+i)) && nd_in_poly(x,y,poly) &&
               (lc[1]!="riparian" || (gk_terrain_height(t,x,y)<wl+3.5 && nd_near_water(t,x,y,12))) &&
               nd_pcg_clear(t,x,y,ls,lsp,lex,lcl,3) && nd_ground_ok(t,x,y,r[2])) [x,y,s]];

module nd_pcg_landcover(t,landcover=[],sites=[],spawns=[],exits=[],clear=[],seed=0,region="bohemia")
{
    wl=t[5];
    if(len(landcover)>0) for(i=[0:len(landcover)-1])
    {
        lc=landcover[i];
        r=nd_lc_recipe(lc[1]);
        poly=lc[2];
        if(r[1]>0)
        {
            bb=nd_bbox(poly);
            tot=nd_sum([for(q=r[5]) q[2]]);
            pts=nd_stand_points(t,lc,i,r,poly,bb,[for(q=sites) if(nd_rect_near(q,bb,6)) q],[for(q=spawns) if(nd_pt_near(q,bb,12)) q],
                                [for(q=exits) if(nd_pt_near(q,bb,24)) q],[for(q=clear) if(nd_seg_near(q,bb,q[2]+6)) q],seed,wl);
            for(pt=pts)
            {
                s=pt[2];
                q=r[4]>0 && nd_u(s,4)<r[4] && nd_edge_dist([pt[0],pt[1]],poly)<6 ? ["hedge","shrub",1,0.9,1.4] : r[5][nd_pick(r[5],nd_u(s,5)*tot)];
                v=floor(nd_u(s,6)*6);
                k=q[3]+(q[4]-q[3])*nd_u(s,7);
                translate([pt[0],pt[1],gk_terrain_height(t,pt[0],pt[1])]) rotate([0,0,nd_u(s,8)*360]) scale([k,k,k])
                    nd_pcg_plant(q[1],v,v*131+len(q[0])*977+7,region,q[0]);
            }
        }
    }
}

// PCG_ALLEYS = [[[[x,y],...],species,spacing,offset],...]: Bohemian roads are
// lined with fruit trees or lindens on both verges; gaps where trees died.
module nd_pcg_alleys(t,alleys=[],sites=[],spawns=[],exits=[],clear=[],seed=0,region="bohemia")
{
    if(len(alleys)>0) for(i=[0:len(alleys)-1])
    {
        al=alleys[i];
        pts=al[0];
        for(g=[0:len(pts)-2])
        {
            a=pts[g]; b=pts[g+1];
            L=norm(b-a);
            n=floor(L/al[2]);
            dir=(b-a)/max(0.01,L);
            nrm=[-dir[1],dir[0]];
            for(k=[0:max(0,n)],side=[-1,1])
            {
                s=seed*17+i*4441+g*331+k*7+(side+1)*3;
                p=a+dir*(k*al[2]+(side>0 ? al[2]*0.5 : 0))+nrm*side*(al[3]+(nd_u(s,1)-0.5)*0.8);
                if(nd_u(s,2)<0.86 && nd_pcg_clear(t,p[0],p[1],sites,spawns,exits,clear,2) && nd_ground_ok(t,p[0],p[1],30))
                {
                    v=floor(nd_u(s,3)*5);
                    translate([p[0],p[1],gk_terrain_height(t,p[0],p[1])]) rotate([0,0,nd_u(s,4)*360])
                        scale([0.9+nd_u(s,5)*0.3,0.9+nd_u(s,5)*0.3,0.9+nd_u(s,5)*0.3])
                            nd_pcg_plant("tree",v,v*131+29,region,al[1]);
                }
            }
        }
    }
}
