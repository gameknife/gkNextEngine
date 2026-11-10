// nextdayz_training.scad — NextDayz training ground assemblies. Each tg_* site
// kind lays out one compound on its own levelled pad (local frame: origin at
// the pad centre, front -y toward the access road). Buildings are
// nd_pcg_building nodes (region "training"); every item in the game is on
// display as an nd_item anchor (always that item, restocked shortly after it
// is taken); nd_target nodes score hits on the range.
//
// Display lists below mirror ItemDefs.cpp; the [NextDayz][Training] unit test
// fails when an item is missing, so add new items here when adding them there.
use <kit_training.scad>
use <kit_coldwar.scad>
use <nextdayz_coldwar.scad>
use <nextdayz_settlement.scad>

// ================= catalogue: [id, label, count] =================
function tg_list_guns() = [["ak","AK-74",1],["aks74u","AKS-74U",1],["svd","SVD",1],["mosin","Mosin",1],["svt40","SVT-40",1],["toz34","TOZ-34",1]];
function tg_list_guns2() = [["shotgun","Shotgun",1],["bizon","PP-19 Bizon",1],["pistol","Makarov",1],["aps","APS Stechkin",1]];
function tg_list_melee() = [["knife","Hunting Knife",1],["crowbar","Crowbar",1],["axe","Fire Axe",1]];
function tg_list_ammo() = [["ammo_545","5.45x39",30],["ammo_762","7.62x54",20],["ammo_12g","12 Gauge",12],["ammo_9mm","9x18",24]];
function tg_list_attach() = [["optic_red_dot","Red Dot",1],["optic_holographic","Holographic",1],["optic_4x","4x Scope",1],["grip_vertical","Foregrip",1],
                             ["mag_quick","Quickdraw Mag",1],["mag_extended","Extended Mag",1],["muzzle_suppressor","Suppressor",1],["tactical_flashlight","Flashlight",1]];
function tg_list_head() = [["helmet","Helmet",1],["helmet_tactical","Tactical Helmet",1],["helmet_ssh68","SSh-68",1],["helmet_m56","M56 Stahlhelm",1],["helmet_pasgt","PASGT",1],
                           ["hat_beanie","Beanie",1],["cap_baseball","Baseball Cap",1],["hat_boonie","Boonie",1],["hat_boonie_emr","EMR Boonie",1],["hat_ushanka","Ushanka",1],["hat_panama","Panama",1]];
function tg_list_torso() = [["jacket","Field Jacket",1],["jacket_afganka","Afganka",1],["jacket_gorka","Gorka",1],["jacket_m81","M81 Woodland",1],["jacket_emr","EMR",1],
                            ["jacket_dbdu","DBDU Desert",1],["jacket_flora","Flora",1],["jacket_strichtarn","Strichtarn",1],["shirt_telnyashka","Telnyashka",1],
                            ["hoodie","Hoodie",1],["jacket_puffer","Puffer",1],["jacket_rain","Rain Shell",1],["jacket_denim","Denim",1],["jacket_rescue","Rescue",1]];
function tg_list_vest() = [["vest_military","Military Vest",1],["vest_6b3","6B3 Armor",1],["vest_police","Police Armor",1],["vest_chest_rig","Chest Rig",1],
                           ["vest_type56","Type 56 Rig",1],["vest_hunting","Hunting Vest",1],["vest_utility","Utility Vest",1],["vest_rescue","Safety Vest",1]];
function tg_list_legs() = [["pants_afganka","Afganka",1],["pants_gorka","Gorka",1],["pants_m81","M81 Woodland",1],["pants_emr","EMR",1],["pants_dbdu","DBDU Desert",1],
                           ["pants_flora","Flora",1],["pants_strichtarn","Strichtarn",1],["pants_tactical","Tactical",1],["cargo_pants","Cargo",1],
                           ["work_pants","Work Pants",1],["jeans","Jeans",1],["pants_rain","Rain Trousers",1],["pants_snow","Snow Pants",1]];
function tg_list_back() = [["backpack_military","Assault Pack",1],["backpack_rd54","RD-54",1],["backpack_abalakov","Abalakov",1],["backpack","Backpack",1],
                           ["backpack_small","Small Pack",1],["backpack_rolltop","Rolltop",1],["backpack_medical","Medic Pack",1],["backpack_messenger","Messenger",1]];
function tg_list_food() = [["food_can","Beans",1],["food_tuna","Tuna",1],["food_crackers","Crackers",1],["food_apple","Apple",1],
                           ["food_chocolate","Chocolate",1],["water_bottle","Water",1],["drink_soda","Soda",1],["water_bottle_empty","Empty Bottle",1]];
function tg_list_med() = [["bandage","Bandage",1],["rag","Rags",6],["medkit","First Aid Kit",1],["painkillers","Painkillers",1]];
function tg_list_tools() = [["can_opener","Can Opener",1],["compass","Compass",1],["map","Map",1],["radio","Field Radio",1],["battery","Battery",4],
                            ["lantern","Lantern",1],["fuel","Fuel",1],["bedroll","Bedroll",1],["firecrackers","Firecrackers",1],["signal_flare","Flare Gun",1]];
// Guns and ammunition issued again at the firing line.
function tg_list_issue() = [["ak","AK-74",1],["svd","SVD",1],["mosin","Mosin",1],["shotgun","Shotgun",1],["bizon","PP-19 Bizon",1],["pistol","Makarov",1]];

function nd_tg_kinds() = ["tg_gate","tg_hq","tg_armory","tg_quartermaster","tg_mess","tg_stores","tg_barracks","tg_range","tg_obstacle","tg_killhouse","tg_motorpool","tg_pen"];
function nd_is_training(kind) = len([for(k=nd_tg_kinds()) if(k==kind) 1])>0;

// ================= gameplay markers =================
// Scored target: the node's origin is the aim point. r > 0 scores rings
// (10 at the centre, 1 at radius r), r = 0 reports a plain hit.
module nd_target(d=100,r=0,kind="bull") { children(); }

// ================= displays =================
// One display table: items laid in a row along x, centred at the origin,
// labels on the front skirt (-y). yaw turns the items on the cloth.
module nd_tg_table(items=[],seed=0,step=0.75,D=1.1,yaw=90)
{
    k=len(items);
    L=k*step+0.3;
    gk_flatten()
    {
        tg_table_display(seed,L,D);
        for(i=[0:k-1]) translate([-L/2+0.15+(i+0.5)*step,-D/2-0.04,tg_table_top()-0.09])
            tg_plate(items[i][1],step-0.05,0.09,0.04,tg_white(),tg_black(),0.01);
    }
    for(i=[0:k-1]) translate([-L/2+0.15+(i+0.5)*step,0.06,tg_table_top()+0.01]) rotate([0,0,yaw]) nd_item(id=items[i][0],n=items[i][2]);
}
// A row of tables of `per` items each, laid end to end along +x from x=0.
function nd_tg_row_len(n,per=6,step=0.75,gap=1.2) = let(T=ceil(n/per)) (T-1)*(per*step+0.3+gap)+(n-(T-1)*per)*step+0.3;
module nd_tg_row(items=[],per=6,step=0.75,D=1.1,gap=1.2,yaw=90,seed=0)
{
    n=len(items);
    for(t=[0:ceil(n/per)-1])
    {
        a=t*per;
        b=min(n,a+per)-1;
        tl=(b-a+1)*step+0.3;
        translate([t*(per*step+0.3+gap)+tl/2,0,0]) nd_tg_table([for(i=[a:b]) items[i]],seed+t*7,step,D,yaw);
    }
}

// ================= buildings (nd_pcg_building, region "training") =================
module nd_tg_building(kind="tg_hq",seed=0,L=12,D=8)
{
    if(kind=="tg_armory")
    {
        // 16 x 10 blockhouse: the arms store behind the display pavilion.
        gk_flatten()
        {
            tg_bldg_armory(seed,L,D);
            for(sx=[-1,1]) translate([sx*L*0.25,D/2-0.34,0.12]) tg_rifle_rack(seed+sx,6);
            translate([0,D/2-0.65,0.12]) cw_prop_locker(seed,3);
            for(sx=[-1,1]) translate([sx*(L/2-0.8),-D/2+0.9,0.12]) tg_ammo_box(seed+sx*3);
            for(i=[0:3]) translate([-L/2+1.2+i*1.3,0.8,0.12]) { tg_crate(seed+i,1.0,0.6,0.5); translate([0,0,0.5]) tg_crate(seed+i+5,1.0,0.6,0.4); }
            for(i=[0:3]) translate([L/2-1.2-i*1.3,0.8,0.12]) tg_crate(seed+i+9,1.0,0.6,0.5);
            translate([0,D/2-1.0,2.0]) tg_plate("SAFETY FIRST  -  CHECK CHAMBER",3.2,0.3,0.14,tg_red(),tg_white(),0.02);
        }
    }
    else if(kind=="tg_arms_display")
    {
        // Open pavilion: long guns and short arms on the back tables, ammunition,
        // attachments and edged weapons along the front.
        gk_flatten()
        {
            tg_pavilion(seed,L,D);
            for(sx=[-1,1]) translate([sx*L*0.25,D/2-0.2,0.12]) tg_rifle_rack(seed+sx,6);
            translate([0,-D/2-0.1,3.2]) tg_plate("ARMS DISPLAY  -  TAKE ONE, TRY IT",5.0,0.36,0.2,tg_yellow(),tg_black(),0.03);
            for(sx=[-1,1]) translate([sx*(L/2-1.2),-D/2+0.8,0.12]) tg_ammo_box(seed+sx);
        }
        translate([-L*0.25,1.6,0.12]) translate([-nd_tg_row_len(6,6,0.78)/2,0,0]) nd_tg_row(tg_list_guns(),6,0.78,1.35,1.2,90,seed);
        translate([L*0.25,1.6,0.12]) translate([-nd_tg_row_len(4,4,1.1)/2,0,0]) nd_tg_row(tg_list_guns2(),4,1.1,1.35,1.2,90,seed+11);
        translate([-L*0.32,-2.0,0.12]) translate([-nd_tg_row_len(4,4,0.8)/2,0,0]) nd_tg_row(tg_list_ammo(),4,0.8,1.0,1.2,0,seed+13);
        translate([0,-2.0,0.12]) translate([-nd_tg_row_len(8,8,0.52)/2,0,0]) nd_tg_row(tg_list_attach(),8,0.52,1.0,1.2,0,seed+17);
        translate([L*0.32,-2.0,0.12]) translate([-nd_tg_row_len(3,3,0.9)/2,0,0]) nd_tg_row(tg_list_melee(),3,0.9,1.0,1.2,90,seed+19);
    }
    else if(kind=="tg_quartermaster")
    {
        // Two issue tents side by side, end-on to the road, sides rolled up
        // for light. Each holds a row along either side of its aisle.
        // L = length (y), D = overall width (x) of the pair.
        w=(D-2)/2;
        gk_flatten()
        {
            for(sx=[-1,1]) translate([sx*(w/2+1),0,0]) rotate([0,0,90]) tg_tent(seed+sx,L,w,2.0,1.8,true);
            translate([0,-L/2-0.4,2.4]) tg_plate("QUARTERMASTER  -  TRY IT ON",3.0,0.3,0.14,tg_yellow(),tg_black(),0.02);
            for(sx=[-1,1]) translate([sx*(w/2+1),L/2-1.2,0.08]) tg_mannequin(seed+sx,tg_canvas(seed+sx));
        }
        rows=[[tg_list_head(),-(w/2+1)-w/2+1.2,1],[tg_list_torso(),-(w/2+1)+w/2-1.2,-1],
              [tg_list_legs(),(w/2+1)-w/2+1.2,1],[concat(tg_list_vest(),tg_list_back()),(w/2+1)+w/2-1.2,-1]];
        // A row facing +x runs north from the south door; one facing -x runs south.
        for(r=rows) translate([r[1],-r[2]*(L/2-3.0),0.08]) rotate([0,0,r[2]*90])
            nd_tg_row(r[0],5,0.9,0.9,1.4,0,seed+round(abs(r[1])*10));
    }
    else if(kind=="tg_mess")
    {
        // Army tent turned end-on: rations on the serving table, benches.
        gk_flatten()
        {
            rotate([0,0,90]) tg_tent(seed,L,D,1.9,1.4,true);
            for(i=[0:2]) translate([1.6,-L/2+4+i*3.2,0.08]) rotate([0,0,90])
            {
                cw_prop_table(seed+i,1.8,0.8);
                for(sy=[-1,1]) translate([0,sy*0.65,0]) tg_m_timber(tg_wood(seed+i)) { translate([0,0,0.42]) tg_box([1.8,0.3,0.05]); for(sx=[-1,1]) translate([sx*0.7,0,0]) tg_post(0.42,0.06); }
            }
            translate([-1.8,L/2-1.6,0.08]) cw_prop_stove(seed);
            translate([-2.2,L/2-3.2,0.08]) tg_crate(seed+3,0.9,0.5,0.45);
        }
        translate([-1.6,-L/2+3.0,0.08]) rotate([0,0,90]) nd_tg_row(tg_list_food(),8,0.7,0.9,1.2,0,seed+5);
    }
    else if(kind=="tg_stores")
    {
        gk_flatten()
        {
            rotate([0,0,90]) tg_tent(seed,L,D,2.0,1.8,true);
            for(i=[0:2]) translate([D/2-1.0,-L/2+3+i*2.6,0.08]) rotate([0,0,90]) tg_shelf(seed+i,2.2,0.55,4);
            for(i=[0:1]) translate([D/2-1.1,-L/2+3+i*2.6,0.08+tg_shelf_z(1)]) rotate([0,0,90]) tg_crate(seed+i,0.8,0.4,0.35);
            translate([D/2-1.4,L/2-2.2,0.08]) rotate([0,0,-90]) cw_prop_workbench(seed);
            translate([0,-L/2-0.3,2.5]) tg_plate("FIELD STORES  -  MEDICAL / TOOLS",3.2,0.3,0.13,tg_yellow(),tg_black(),0.02);
        }
        translate([-1.4,-L/2+2.6,0.08]) rotate([0,0,90]) nd_tg_row(tg_list_med(),4,0.8,1.0,1.2,0,seed+3);
        translate([-1.4,-L/2+7.4,0.08]) rotate([0,0,90]) nd_tg_row(tg_list_tools(),5,0.85,1.0,1.2,0,seed+7);
    }
    else if(kind=="tg_hq")
    {
        gk_flatten()
        {
            tg_bldg_hq(seed,L,D);
            for(sx=[-1,1]) translate([sx*L*0.3,1.2,0.12]) { cw_prop_table(seed+sx,1.6,0.8); translate([0,0.75,0]) cw_prop_chair(seed+sx); }
            translate([0,1.5,0.12]) { tg_table_display(seed,2.6,1.6); translate([0,0,0.87]) tg_m_paper([0.36,0.38,0.30]) tg_box([2.3,1.3,0.005]); }
            translate([0,D/2-0.6,0.12]) rotate([0,0,180]) tg_notice_board(seed,"TRAINING SCHEDULE");
            for(sx=[-1,1]) translate([sx*(L/2-0.8),D/2-0.7,0.12]) cw_prop_locker(seed+sx,2);
            translate([L*0.3,1.2,0.95]) cw_item_radio();
        }
    }
    else if(kind=="tg_barracks")
    {
        gk_flatten()
        {
            tg_bldg_barracks(seed,L,D);
            for(i=[-4:4]) if(i!=0) for(sy=[-1,1]) translate([i*L/10,sy*(D/2-1.4),0.12]) rotate([0,0,sy<0?0:180])
            {
                cw_prop_bed(seed+i*3+sy);
                translate([0,0,0.75]) cw_prop_bed(seed+i*5+sy);   // bunk
                for(sx=[-1,1]) translate([sx*0.95,0,0]) tg_post(1.6,0.05);
                translate([1.25,0.2,0]) tg_ammo_box(seed+i);
            }
            for(sx=[-1,1]) translate([sx*(L/2-0.9),0,0.12]) rotate([0,0,sx*90]) cw_prop_locker(seed+sx,3);
            translate([0,0,0.12]) cw_prop_table(seed,2.4,0.9);
        }
    }
    else if(kind=="tg_guardhouse")
        gk_flatten() { tg_bldg_guardhouse(seed,L,D); translate([0,D/2-0.8,0.12]) cw_prop_table(seed,1.4,0.6); translate([0,D/2-1.4,0.12]) cw_prop_chair(seed); }
    else if(kind=="tg_hut")
        gk_flatten() { tg_shell(seed,L,D,2.9,"render",1,[],[0],1.0,false,false,-1); translate([L/2-1,D/2-0.6,0.12]) cw_prop_locker(seed,2); }
    else if(kind=="tg_signals")
        gk_flatten() { tg_shell(seed,L,D,2.9,"render",0,[-L*0.25,L*0.25],[],1.0,false,true,0); translate([0,D/2-0.7,0.12]) cw_prop_workbench(seed); cw_prop_antenna(seed); }
    else if(kind=="tg_latrine")
        gk_flatten() { tg_shell(seed,L,D,2.7,"brick",2,[],[0],1.0,false,false,1); for(i=[-1:1]) translate([i*L/4,D/2-0.8,0.12]) tg_m_concrete([0.40,0.40,0.38]) translate([0,0,0.2]) tg_box([0.6,0.8,0.4]); }
    else if(kind=="tg_workshop")
        gk_flatten()
        {
            tg_shell(seed,L,D,4.2,"brick",2,[-L*0.3,L*0.3],[0],2.2,false,true,-1,tg_redbrick(seed));
            translate([L/2-1.6,D/2-0.6,0.12]) cw_prop_workbench(seed);
            translate([-L/2+1.0,D/2-1.0,0.12]) cw_prop_tires(seed);
            translate([-L/2+1.2,-D/2+1.2,0.12]) cw_prop_barrel(seed);
            translate([0,1,0.12]) cw_veh_uaz_van(seed);
        }
    else if(kind=="tg_vehicle_shed")
        gk_flatten()
        {
            tg_vehicle_shed(seed,L,D);
            translate([-L/3,0,0.1]) rotate([0,0,90]) cw_veh_truck_canvas(seed);
            translate([0,0,0.1]) rotate([0,0,90]) cw_veh_btr(seed+1);
            translate([L/3,0,0.1]) rotate([0,0,90]) cw_veh_truck_canvas(seed+2);
        }
    else if(kind=="tg_tower")
        gk_flatten() tg_bldg_range_tower(seed);
    else if(kind=="tg_firing_shed")
        gk_flatten() tg_firing_shed(seed,8,4.0,D);
    else if(kind=="tg_pistol_shed")
        gk_flatten() tg_firing_shed(seed,3,3.0,D);
    else if(kind=="tg_killhouse") nd_tg_killhouse(seed,L,D);
    else if(kind=="tg_shelter")
        gk_flatten() { tg_camo_net(seed,L,D,2.8); translate([0,0,0]) cw_prop_bench(); translate([0,1.2,0]) cw_prop_table(seed,1.4,0.7); }
    else gk_flatten() tg_shell(seed,L,D,3.0,"render",1,[],[0],1.2,false,false,0);
}

// Shoot house: plywood rooms without a roof, figure targets in every room.
// Entry from the front (-y) centre; L x D outer.
module nd_tg_killhouse(seed=0,L=18,D=14)
{
    gk_flatten()
    {
        tg_m_concrete(tg_concrete(seed)) tg_slab(L+0.4,D+0.4,0.08);
        translate([0,-D/2,0.08]) tg_ply_wall(seed,L,2.4,1.2);
        translate([0,D/2,0.08]) tg_ply_wall(seed+1,L,2.4,0);
        for(sx=[-1,1]) translate([sx*L/2,0,0.08]) rotate([0,0,90]) tg_ply_wall(seed+2+sx,D,2.4,sx<0?1.0:0);
        // corridor walls with doorways, four rooms
        for(sx=[-1,1]) translate([sx*1.6,-D/2+D*0.32,0.08]) rotate([0,0,90]) tg_ply_wall(seed+5+sx,D*0.64,2.4,1.0);
        for(sx=[-1,1]) translate([sx*(L/4+0.8),0,0.08]) tg_ply_wall(seed+8+sx,L/2-1.6,2.4,1.0);
        translate([0,D*0.18,0.08]) tg_ply_wall(seed+11,3.2,2.4,1.0);
        // furniture cover in the rooms
        translate([-L/4-1,D/4+0.5,0.08]) cw_prop_table(seed,1.4,0.8);
        translate([L/4+1,-D/4,0.08]) cw_prop_shelf_wood(seed,1.6);
        translate([L/4+1.5,D/4,0.08]) tg_crate(seed+3,1.2,0.8,0.8);
        translate([-L/4,-D/4-1,0.08]) cw_prop_barrel(seed);
        translate([0,-D/2-1.5,0]) tg_sandbag_row(seed,3,3);
        translate([0,-D/2-0.06,2.0]) tg_plate("SHOOT HOUSE",2.0,0.35,0.2,tg_red(),tg_white(),0.03);
    }
    // targets: one per room plus the corridor end
    tp=[[-L/4-1.5,D/4+1.6,0],[L/4+1.6,D/4+1.4,0],[-L/4-1.6,-D/4-1.5,90],[L/4+2.2,-D/4-0.8,-90],[0,D/2-0.6,0],[-L/2+1.0,0.6,90]];
    for(i=[0:len(tp)-1]) translate([tp[i][0],tp[i][1],0.08]) rotate([0,0,tp[i][2]])
    {
        gk_flatten() tg_target_frame(seed+i,"figure",1.25);
        translate([0,0,1.25]) nd_target(d=5,r=0,kind="figure") gk_flatten() tg_face_figure(seed+i);
    }
}

// A scored target at distance d (lane origin at its foot), face toward -y.
module nd_tg_lane_target(kind="bull",d=25,seed=0)
{
    if(kind=="plate")
    {
        gk_flatten() tg_plate_stand(seed,1.0,0.3);
        translate([0,0,1.0]) nd_target(d=d,r=0,kind="plate") gk_flatten() tg_face_plate(0.3);
    }
    else if(kind=="figure")
    {
        gk_flatten() tg_target_frame(seed,"figure",1.2);
        translate([0,0,1.2]) nd_target(d=d,r=0,kind="figure") gk_flatten() tg_face_figure(seed);
    }
    else
    {
        r=d<=25 ? 0.35 : 0.45;
        gk_flatten() tg_target_frame(seed,"bull",1.5);
        translate([0,0,1.5]) nd_target(d=d,r=r,kind="bull") gk_flatten() tg_face_bull(r);
    }
    // a low mound in front protects the frame
    gk_flatten() translate([0,-3.0,0]) rotate([0,0,90]) tg_m_earth() rotate([90,0,90]) linear_extrude(height=3.2,center=true) polygon([[-1.6,0],[1.6,0],[0.4,0.6],[-0.4,0.6]]);
}

// ================= sites =================
// Yard clutter per site kind: [group, x, y, rotation] in the site frame
// (tg_clutter groups), placed clear of buildings, aisles and doors.
function nd_tg_site_props(type,W,H) = let(yf=-H/2+25)
    type=="tg_gate" ? [["sandbags",-24,-6,0],["drums",22,-2,0],["lamp",-6,-14,0],["lamp",6,-14,0],["crates",26,-16,0],["tent",-24,-18,0],["uaz",14,-1,90]] :
    type=="tg_hq" ? [["uaz",22,12,0],["uaz",28,12,0],["net",-45,22,0],["crates",-30,25,0],["drums",-48,8,0],["trash",15,28,0],["truck",44,4,90]] :
    type=="tg_armory" ? [["hesco",-20,6,90],["hesco",20,6,90],["sandbags",-18,-10,0],["net",19,-11,0],["lamp",-12,-16,0],["lamp",12,-16,0],["truck",-21,18,0],["drums",16,16,0]] :
    type=="tg_quartermaster" ? [["crates",18,10,0],["net",-20,18,0],["drums",20,-18,0],["lamp",-9,-22,0],["lamp",9,-22,0],["tent",20,22,0]] :
    type=="tg_mess" ? [["firepit",16,14,0],["woodpile",-18,12,0],["drums",-20,-16,0],["crates",18,-16,0],["trash",-8,16,0]] :
    type=="tg_stores" ? [["cable",-18,-2,0],["crates",18,-14,0],["truck",-20,-16,0],["drums",18,16,0],["net",-19,16,0]] :
    type=="tg_barracks" ? [["tent",-20,28,0],["tent",-8,28,0],["tent",4,28,0],["tent",16,28,0],["firepit",30,26,0],["truck",-50,-10,90],["uaz",-50,-22,90],
                           ["lamp",-45,-6,0],["lamp",15,-6,0],["lamp",45,-6,0],["drums",35,-30,0],["crates",-30,-30,0],["sandbags",0,-30,0]] :
    type=="tg_range" ? [["truck",-32,yf-20,90],["uaz",-32,yf-30,90],["sandbags",36,yf-14,0],["drums",-44,yf-24,0],["crates",22,yf-22,0],["lamp",-24,yf-6,0],["lamp",24,yf-6,0]] :
    type=="tg_obstacle" ? [["tent",18,-55,0],["firepit",18,-40,0],["crates",-18,60,0],["drums",-18,40,0],["lamp",10,0,0]] :
    type=="tg_killhouse" ? [["net",-17,-15,0],["sandbags",0,-17,0],["drums",18,14,0],["crates",-18,14,0]] :
    type=="tg_motorpool" ? [["truck",-25,5,90],["truck",-15,5,90],["uaz",22,8,0],["uaz",28,8,0],["drums",36,14,0],["crates",-36,20,0],["hesco",-40,-30,0],
                            ["cable",10,-28,0],["net",35,-30,0],["trash",-20,-28,0],["lamp",-30,-5,0],["lamp",30,-5,0]] :
    type=="tg_pen" ? [["drums",-24,-20,0],["sandbags",14,-21,0],["lamp",22,-22,0]] : [];
// Concrete hardstanding laid on the levelled pad: [x, y, L, D].
function nd_tg_site_slabs(type,W,H) =
    type=="tg_hq" ? [[0,-H/2+4+H*0.21,W*0.62,H*0.42]] :
    type=="tg_gate" ? [[0,-5,9,30]] :
    type=="tg_armory" ? [[0,2.5,8,10]] :
    type=="tg_motorpool" ? [[0,4,72,22],[0,23,26,10]] : [];

module nd_tg_site(id="site",type="tg_hq",pad=[60,50],count=1,seed=0)
{
    W=pad[0]; H=pad[1];
    props=nd_tg_site_props(type,W,H);
    slabs=nd_tg_site_slabs(type,W,H);
    gk_flatten()
    {
        if(len(props)>0) for(i=[0:len(props)-1]) translate([props[i][1],props[i][2],0]) rotate([0,0,props[i][3]]) tg_clutter(props[i][0],seed+i*13);
        if(len(slabs)>0) for(s=slabs) translate([s[0],s[1],0]) tg_m_concrete(tg_concrete(seed)) tg_slab(s[2],s[3],0.05);
    }
    if(type=="tg_gate")
    {
        // Road runs north through x=0. Guardhouse west, booth east.
        translate([-9,2,0]) nd_pcg_building("tg_guardhouse",seed,"training",4.2,3.6);
        if(count>1) translate([8,-5,0]) rotate([0,0,180]) nd_pcg_building("tg_guardhouse",seed+1,"training",3.0,2.6);
        gk_flatten()
        {
            translate([-4.2,-6,0]) tg_barrier(seed,3.8,72);
            translate([4.2,-6,0]) rotate([0,0,180]) tg_barrier(seed+1,3.8,0);
            for(sx=[-1,1]) translate([sx*(6+14),9,0]) tg_fence_panel(seed+sx,7);
            for(sx=[-1,1]) translate([sx*4.6,8,0]) tg_m_concrete(tg_concrete(seed)) translate([0,0,1.6]) tg_box([0.8,0.8,3.2]);
            translate([0,8,3.5]) tg_plate("TRAINING AREA 7",9,0.8,0.5,tg_red(),tg_white(),0.12);
            for(sx=[-1,1]) translate([sx*4.6,8.45,1.8]) rotate([0,0,180]) tg_plate("STOP",0.7,0.7,0.3,tg_red(),tg_white(),0.03);
            translate([15,-11,0]) cw_prop_sandbag_nest(seed);
            for(i=[0:3]) translate([-14+i*1.8,-16-(i%2)*1.2,0]) cw_prop_hedgehog();
            for(i=[0:2]) translate([10+i*2.4,-20,0]) rotate([0,0,30]) cw_prop_hedgehog();
            translate([-16,-14,0]) { tg_m_timber(tg_wood(seed)) for(sx=[-1,1]) translate([sx*1.4,0,0]) tg_post(2.6,0.12); translate([0,-0.08,1.9]) tg_plate("MILITARY TRAINING AREA",3.2,1.0,0.22,tg_white(),tg_red(),0.04); translate([0,-0.08,1.3]) tg_plate("LIVE FIRING  -  NO ENTRY",3.2,0.3,0.16,tg_red(),tg_white(),0.03); }
            translate([-14,8,0]) tg_flagpole(seed,9);
            translate([12,4,0]) tg_floodlight(seed,7);
            for(sy=[-1,1]) translate([0,sy*3-12,0.02]) tg_m_paint(tg_yellow()) tg_box([7.5,0.35,0.06]);   // speed bumps
            translate([-9,-2.5,0]) tg_sandbag_row(seed+3,4.2,2);
        }
    }
    else if(type=="tg_hq")
    {
        translate([0,H/2-14,0]) nd_pcg_building("tg_hq",seed,"training",20,10);
        if(count>1) translate([W/2-12,H/2-12,0]) nd_pcg_building("tg_signals",seed+3,"training",7,5);
        gk_flatten()
        {
            // parade ground: kerbed, white rocks, three flags, a reviewing stand
            px=W*0.62; py=H*0.42; pc=[0,-H/2+4+py/2];
            translate(pc) { for(sy=[-1,1]) translate([0,sy*py/2,0]) tg_kerb(px); for(sx=[-1,1]) translate([sx*px/2,0,0]) rotate([0,0,90]) tg_kerb(py); }
            translate(pc) for(i=[0:15]) translate([-px/2+i*px/15,-py/2-1.2,0]) tg_white_rock(seed+i);
            translate(pc) tg_m_paint([0.45,0.45,0.43]) for(i=[-3:3]) translate([i*px/8,0,0.005]) tg_box([0.12,py-2,0.01]);
            for(i=[-1:1]) translate([i*3.5,pc[1]+py/2+3,0]) tg_flagpole(seed+i,i==0?11:9,i==0?tg_red():tg_green(seed+i));
            translate([-px/2-5,pc[1],0]) rotate([0,0,-90]) { tg_m_timber(tg_wood(seed)) for(k=[0:2]) translate([0,k*0.8,0.25+k*0.4]) tg_box([6,0.8,0.5+k*0.8]); tg_m_tin(tg_roofgreen(seed)) translate([0,1.2,3.6]) tg_box([6.6,3.0,0.05]); tg_m_steel(tg_green(seed)) for(sx=[-1,1]) translate([sx*3,2.2,0]) tg_post(3.6,0.1); }
            for(sx=[-1,1]) for(sy=[-1,1]) translate([pc[0]+sx*(px/2+1),pc[1]+sy*(py/2+1),0]) tg_floodlight(seed+sx+sy,8);
            translate([px/2+7,pc[1]-2,0]) { tg_m_concrete(tg_concrete(seed)) translate([0,0,0.6]) tg_box([8,4,1.2]); translate([0,0,1.2]) cw_veh_btr(seed); translate([0,-2.05,0.6]) tg_plate("GLORY TO THE DEFENDERS",5,0.3,0.18,tg_yellow(),tg_black(),0.02); }
            translate([-W/2+8,H/2-8,0]) tg_notice_board(seed+3,"DUTY ROSTER");
            translate([-8,H/2-26,0]) tg_notice_board(seed+5,"TRAIN HARD - FIGHT EASY");
            for(i=[0:3]) translate([-W/2+6+i*3,-H/2+3,0]) cw_prop_bench();
        }
    }
    else if(type=="tg_armory")
    {
        translate([0,12,0]) nd_pcg_building("tg_armory",seed,"training",16,10);
        if(count>1) translate([0,-7,0]) nd_pcg_building("tg_arms_display",seed+1,"training",20,9);
        gk_flatten()
        {
            for(sx=[-1,1]) translate([sx*3.4,5.6,0]) rotate([0,0,90]) tg_sandbag_row(seed+sx,2.4,4);
            translate([-13,3,0]) { tg_crate(seed,1.0,0.6,0.5); translate([0,0,0.5]) tg_crate(seed+1,1.0,0.6,0.5); translate([1.2,0,0]) tg_crate(seed+2,1.0,0.6,0.5); }
            translate([11,3,0]) for(i=[0:5]) translate([(i%3)*0.34,floor(i/3)*0.2,0]) tg_ammo_box(seed+i);
            // unloading bay: sand-filled barrel at 45 degrees
            translate([6,3.5,0]) { tg_m_steel(tg_green(seed)) translate([0,0,0.5]) rotate([45,0,0]) cylinder(h=0.9,r=0.29,center=true,$fn=12); translate([0,-0.6,1.4]) tg_plate("CLEARING BAY",1.0,0.2,0.09,tg_red(),tg_white(),0.02); }
            for(sx=[-1,1]) translate([sx*13,-H/2+3,0]) tg_fence_mesh(seed+sx,4);
            translate([-9,-H/2+3,0]) tg_floodlight(seed,7);
        }
    }
    else if(type=="tg_quartermaster")
    {
        translate([0,2,0]) nd_pcg_building("tg_quartermaster",seed,"training",28,16);
        gk_flatten()
        {
            translate([-20,-2,0]) rotate([0,0,90]) tg_bldg_nissen(seed+7,16,8);
            for(i=[0:2]) translate([-15+i*1.2,-16.5,0]) tg_mannequin(seed+i,tg_canvas(seed+i*3));
            translate([13,-16,0]) tg_clothes_rail(seed,2.0,7);
            translate([13,-6,0]) for(i=[0:3]) translate([0,i*1.0,0]) tg_crate(seed+i,1.0,0.7,0.6);
            translate([15,16,0]) { for(sx=[-1,1]) translate([sx*3,0,0]) tg_post(2.0,0.08); translate([0,0,1.9]) tg_rod([-3,0,0],[3,0,0],0.01); for(i=[0:4]) gk_material(tg_canvas(seed+i),roughness=1) gk_texture("canvas",scale=0.4) translate([-2.4+i*1.2,0,1.5]) tg_box([0.7,0.03,0.7]); }
        }
    }
    else if(type=="tg_mess")
    {
        translate([0,4,0]) nd_pcg_building("tg_mess",seed,"training",16,6.5);
        gk_flatten()
        {
            // field kitchen under a camo net, wash stand, water trailer
            translate([-12,-4,0]) { tg_camo_net(seed,7,5,2.8); translate([-1.5,0.5,0]) cw_prop_stove(seed); translate([1.2,0.5,0]) cw_prop_table(seed,1.6,0.8); translate([0,-1.5,0]) cw_prop_barrel(seed); }
            translate([11,-2,0]) { tg_m_steel(tg_green(seed)) { translate([0,0,1.1]) rotate([0,90,0]) cylinder(h=3,r=0.6,center=true,$fn=14); translate([2,0,0.6]) tg_box([1.6,0.08,0.08]); } tg_m_rubber() for(sy=[-1,1]) translate([0,sy*0.75,0.38]) rotate([90,0,0]) cylinder(h=0.25,r=0.38,center=true,$fn=12); }
            translate([10,6,0]) { tg_m_steel([0.30,0.30,0.29]) translate([0,0,0.8]) tg_box([3,0.5,0.2]); for(sx=[-1,1]) translate([sx*1.3,0,0]) tg_post(0.8,0.08); }
            for(i=[0:2]) translate([-6+i*6,-H/2+6,0]) { cw_prop_table(seed+i,2.0,0.8); for(sy=[-1,1]) translate([0,sy*0.7,0]) cw_prop_bench(); }
        }
    }
    else if(type=="tg_stores")
    {
        translate([0,1,0]) nd_pcg_building("tg_stores",seed,"training",20,8);
        gk_flatten()
        {
            translate([-9,-6,0]) for(i=[0:5]) translate([(i%3)*0.7,floor(i/3)*0.7,0]) cw_prop_barrel(seed+i);
            translate([9,-5,0]) { cw_prop_pallet(seed); translate([0,0,0.15]) tg_crate(seed,1.1,0.9,0.7); translate([0,1.5,0]) cw_prop_pallet(seed+1); }
            translate([9,8,0]) { tg_m_steel(tg_green(seed)) translate([0,0,0.6]) tg_box([2.0,1.0,1.2]); tg_m_rubber() for(sx=[-1,1]) translate([sx*0.7,0,0.25]) rotate([90,0,0]) cylinder(h=1.1,r=0.25,center=true,$fn=10); }   // generator
            translate([-9,8,0]) cw_prop_tent_medical(seed+2);
        }
    }
    else if(type=="tg_barracks")
    {
        n=max(1,count-1);
        for(i=[0:n-1]) translate([(i-(n-1)/2)*30,6,0]) nd_pcg_building("tg_barracks",seed+i*17,"training",24,9);
        if(count>1) translate([W/2-10,H/2-12,0]) nd_pcg_building("tg_latrine",seed+5,"training",8,5);
        gk_flatten()
        {
            translate([-W/2+10,H/2-12,0]) cw_bldg_water_tower(seed);
            for(i=[0:n-1]) translate([(i-(n-1)/2)*30,-6,0]) { tg_kerb(20); for(sx=[-1,1]) translate([sx*6,-3,0]) cw_prop_bench(); }
            translate([W/2-10,-H/2+10,0]) { tg_ob_bars(seed,4); translate([6,0,0]) tg_ob_beam(seed,4); }
            translate([W/2-22,H/2-9,0]) { tg_m_concrete(tg_concrete(seed)) translate([0,0,0.45]) tg_box([5,0.8,0.9]); tg_m_zinc() for(i=[-2:2]) translate([i,0,1.1]) cylinder(h=0.3,r=0.02,$fn=4); }
            translate([-W/2+12,-H/2+8,0]) { tg_camo_net(seed+2,6,4,2.6); cw_prop_bench(); translate([0,0,0]) cw_prop_barrel(seed+1); }
            for(i=[0:5]) translate([-W/2+24+i*3,H/2-6,0]) { tg_post(2.0,0.06); if(i<5) gk_material(tg_canvas(seed+i),roughness=1) translate([1.5,0,1.6]) tg_box([1.0,0.02,0.6]); }
        }
    }
    else if(type=="tg_range")
    {
        // Firing line near the south end, shooters face +y. Eight rifle lanes
        // of 4 m at increasing distances, a pistol bay east, berms on the
        // flanks and a backstop berm behind the 300 m line.
        yf=-H/2+25;
        lanes=[["bull",25],["bull",50],["figure",100],["plate",100],["figure",150],["plate",200],["figure",250],["plate",300]];
        translate([0,yf-2.5,0]) nd_pcg_building("tg_firing_shed",seed,"training",32,5);
        if(count>1) translate([28,yf-2.5,0]) nd_pcg_building("tg_pistol_shed",seed+1,"training",9,5);
        if(count>2) translate([-W/2+9,yf-9,0]) nd_pcg_building("tg_tower",seed+2,"training",6,6);
        for(i=[0:len(lanes)-1]) translate([-16+(i+0.5)*4,yf+lanes[i][1],0]) nd_tg_lane_target(lanes[i][0],lanes[i][1],seed+i*7);
        pistol=[["plate",7],["figure",10],["bull",15]];
        for(i=[0:2]) translate([28-3+i*3,yf+pistol[i][1],0]) nd_tg_lane_target(pistol[i][0],pistol[i][1],seed+50+i);
        gk_flatten()
        {
            translate([0,yf+325,0]) tg_berm(W-16,8,3,18);
            translate([-22,yf+150,0]) rotate([0,0,90]) tg_berm(310,3,1.5,7);
            translate([40,yf+150,0]) rotate([0,0,90]) tg_berm(310,3,1.5,7);
            translate([28,yf+21,0]) tg_berm(12,2.4,1.2,5);
            for(d=[25,50,100,150,200,250,300]) translate([-19.5,yf+d,0]) tg_distance_marker(d);
            for(i=[6,7]) translate([-16+(i+0.5)*4,yf-1.2,0.12]) tg_shooting_bench(seed+i);
            // range entry signs and the issue point behind the line
            translate([-W/2+16,yf-16,0]) tg_notice_board(seed,"RANGE RULES");
            translate([12,yf-16,0]) { tg_m_timber(tg_wood(seed)) for(sx=[-1,1]) translate([sx*1.4,0,0]) tg_post(2.6,0.12); translate([0,-0.08,1.9]) tg_plate("STOP! LIVE FIRING",3.2,0.9,0.3,tg_red(),tg_white(),0.04); }
            translate([0,yf-14,0]) tg_camo_net(seed+5,16,7,3.0);
            for(i=[0:3]) translate([-8+i*1.4,yf-17.5,0]) tg_ammo_box(seed+i);
            translate([W/2-10,yf-14,0]) tg_floodlight(seed,8);
        }
        translate([-nd_tg_row_len(6,6,0.85)/2,yf-12.5,0]) nd_tg_row(tg_list_issue(),6,0.85,1.35,1.2,90,seed+9);
        translate([-nd_tg_row_len(4,4,0.8)/2,yf-15.5,0]) nd_tg_row(tg_list_ammo(),4,0.8,1.0,1.2,0,seed+13);
    }
    else if(type=="tg_obstacle")
    {
        // A dirt track runs north; stations every 15 m.
        if(count>0) translate([W/2-10,-H/2+12,0]) nd_pcg_building("tg_shelter",seed,"training",6,4);
        y0=-H/2+14;
        gk_flatten()
        {
            translate([0,y0,0]) tg_ob_wall(seed,1.2);
            translate([0,y0+14,0]) rotate([0,0,90]) tg_ob_beam(seed+1,7);
            translate([0,y0+28,0]) tg_ob_tires(seed+2,10);
            translate([0,y0+44,0]) rotate([0,0,90]) tg_ob_net(seed+3,9,3);
            translate([0,y0+60,0]) rotate([0,0,90]) tg_ob_bars(seed+4,6);
            translate([0,y0+75,0]) tg_ob_ladder(seed+5,4);
            translate([0,y0+90,0]) tg_ob_wall(seed+6,2.0);
            translate([0,y0+106,0]) rotate([0,0,90]) tg_ob_trench(seed+7,10);
            translate([0,y0+122,0]) rotate([0,0,90]) tg_ob_beam(seed+8,9);
            translate([0,y0+136,0]) tg_ob_wall(seed+9,1.6);
            translate([0,y0+148,0]) { tg_m_timber(tg_wood(seed)) for(sx=[-1,1]) translate([sx*2.2,0,0]) tg_post(3.2,0.18); translate([0,0,3.1]) tg_plate("FINISH",4.6,0.6,0.35,tg_red(),tg_white(),0.06); }
            for(i=[0:8]) translate([-6,y0+i*17,0]) tg_white_rock(seed+i);
            for(i=[0:8]) translate([6,y0+8+i*17,0]) tg_white_rock(seed+i+20);
            translate([-W/2+8,-H/2+10,0]) tg_notice_board(seed,"OBSTACLE COURSE");
            translate([-W/2+12,0,0]) for(k=[0:2]) tg_m_timber(tg_wood(seed+k)) translate([0,k*0.8-0.8,0.2+k*0.35]) tg_box([5,0.7,0.4+k*0.7]);   // bleachers
        }
    }
    else if(type=="tg_killhouse")
    {
        translate([0,2,0]) nd_pcg_building("tg_killhouse",seed,"training",18,14);
        gk_flatten()
        {
            translate([-12,-12,0]) tg_notice_board(seed,"CQB DRILLS");
            translate([10,-12,0]) for(i=[0:2]) translate([i*1.4,0,0]) tg_ammo_box(seed+i);
            for(sx=[-1,1]) translate([sx*(18/2+4),2,0]) rotate([0,0,90]) tg_hesco(seed+sx,6);
        }
    }
    else if(type=="tg_motorpool")
    {
        translate([0,H/2-12,0]) nd_pcg_building("tg_vehicle_shed",seed,"training",24,9);
        if(count>1) translate([-W/2+12,-H/2+14,0]) nd_pcg_building("tg_workshop",seed+1,"training",12,9);
        gk_flatten()
        {
            translate([W/2-14,-H/2+14,0]) tg_fuel_point(seed);
            translate([6,-6,0]) rotate([0,0,25]) cw_veh_tank(seed);
            translate([-6,-14,0]) rotate([0,0,-10]) cw_veh_uaz_van(seed+3);
            for(i=[0:3]) translate([-W/2+4,-4+i*2.2,0]) cw_prop_tires(seed+i);
            for(i=[0:5]) translate([W/2-6+(i%2)*0.7,4+floor(i/2)*0.7,0]) cw_prop_barrel(seed+i);
            for(i=[0:2]) translate([-2+i*5,H/2-22,0.01]) tg_m_gloss([0.03,0.03,0.03]) scale([1.2,0.8,1]) cylinder(h=0.005,r=1,$fn=10);   // oil stains
            translate([0,-H/2+3,0]) tg_floodlight(seed,8);
        }
    }
    else if(type=="tg_pen")
    {
        // Closed concrete pen for live practice on the infected; a tower at
        // the south wall overlooks it.
        S=min(W,H)-24;
        c=[0,6];
        translate([0,c[1]-S/2-6,0]) nd_pcg_building("tg_tower",seed,"training",6,6);
        gk_flatten()
        {
            translate(c) for(r=[0,90,180,270]) rotate([0,0,r]) translate([0,-S/2,0]) tg_fence_panel(seed+r,ceil(S/4));
            translate([c[0]+S/2-6,c[1]-S/2-0.2,1.6]) tg_plate("DANGER - INFECTED",3.0,0.6,0.26,tg_red(),tg_white(),0.03);
            translate([c[0]-S/2+6,c[1]-S/2-0.2,1.6]) tg_plate("LIVE TARGETS - DO NOT ENTER",3.6,0.5,0.18,tg_yellow(),tg_black(),0.03);
            for(i=[0:3]) translate([c[0]-S/2+4+i*(S-8)/3,c[1]+S/2-3,0]) cw_prop_body_bag(seed+i);
        }
        for(p=[[-S/4,S/4],[S/4,S/4],[0,S/2-6]]) translate([c[0]+p[0],c[1]+p[1],0.12]) nd_zed(p="military",group=2);
    }
}

// Site terrain: one levelled plot per site with a working surface, plus paints.
function nd_tg_surface(kind) = kind=="tg_range" || kind=="tg_obstacle" || kind=="tg_pen" || kind=="tg_barracks" ? "grass" : "road";
function nd_tg_paints(s) = let(k=s[2],W=s[4][0],H=s[4][1])
    k=="tg_hq" ? [["paint",[[-W*0.31,-H/2+4],[W*0.31,-H/2+4],[W*0.31,-H/2+4+H*0.42],[-W*0.31,-H/2+4+H*0.42]],"pad"]] :
    k=="tg_range" ? [["paint",[[-W/2+4,-H/2+8],[W/2-4,-H/2+8],[W/2-4,-H/2+30],[-W/2+4,-H/2+30]],"road"],
                     ["paint",[[-18,-H/2+30],[-14,-H/2+30],[-14,H/2-20],[-18,H/2-20]],"dry_grass"]] :
    k=="tg_obstacle" ? [["paint",[[-4,-H/2+6],[4,-H/2+6],[4,H/2-6],[-4,H/2-6]],"road"]] :
    k=="tg_motorpool" || k=="tg_gate" ? [["paint",[[-W/2+3,-H/2+3],[W/2-3,-H/2+3],[W/2-3,H/2-3],[-W/2+3,H/2-3]],"pad"]] :
    k=="tg_pen" ? [["paint",[[-W/2+12,-W/2+12+6],[W/2-12,-W/2+12+6],[W/2-12,W/2-12+6],[-W/2+12,W/2-12+6]],"road"]] : [];
function nd_tg_terrain(sites) = [for(s=sites) if(nd_is_training(s[2])) for(f=concat(
    [["pad",s[3],s[4],s[5],6,nd_tg_surface(s[2])]],
    [for(p=nd_tg_paints(s)) ["paint",[for(q=p[1]) nd_xf(q,s[3],s[5])],p[2]]])) f];
