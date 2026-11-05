// Tactical level PCG. Geography supplies terrain/routes/district reference only.
// Site=[id,name,kind,center,padSize,rotation,buildingCount,risk].
use <kit_layout.scad>
use <kit_terrain.scad>
use <kit_coldwar.scad>
use <kit_abandoned.scad>
use <nextdayz_coldwar.scad>

function nd_pcg_pads(sites,spawns,exits) = concat(
    [for(p=sites) ["pad",p[3],p[4],p[5]]],
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

// Coldwar owns furniture + typed loot positions. Only the regional shell changes.
module nd_pcg_building(kind="cottage",seed=0,region="bohemia",L=10,D=8,p="residential")
{
    if(kind=="cottage")
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
    // The entire encounter shares one large pad and one datum, as in Riverland.
    // All building footprints stay >=8 m inside that pad's edge.
    translate([site[3][0],site[3][1],gk_terrain_height(t,site[3][0],site[3][1])]) rotate([0,0,site[5]])
        nd_pcg_encounter(site[0],site[1],site[2],site[4],site[6],site[7],region,seed);
}

module nd_pcg_encounter(id="site",name="Site",type="village",pad=[76,60],count=6,risk=1,region="bohemia",seed=0)
{
    nd_poi(name=name,kind=type=="medical" ? "hospital" : type=="military" ? "military" : "village");
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

module nd_pcg_plant(kind="tree",variant=0,seed=0,region="bohemia")
{
    gk_flatten()
        if(kind=="dead") cw_nature_tree_dead(seed=seed);
        else if(kind=="shrub") lay_pick(i=variant) { cw_nature_bush(seed=seed); cw_nature_grass_tuft(seed=seed); cw_nature_stump(seed=seed); }
        else if(kind=="bamboo") az_nature_bamboo(seed);
        else if(region=="sichuan") lay_pick(i=variant) { az_nature_camphor(seed); az_nature_willow(seed); az_nature_camphor(seed+1); }
        else lay_pick(i=variant) { cw_nature_pine(seed=seed); cw_nature_birch(seed=seed); cw_nature_pine(seed=seed+1); }
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
