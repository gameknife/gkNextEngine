// kit_training.scad — an East-European army training area, the kind a DayZ map
// hides behind a concrete fence: silicate-brick HQ and barracks with green
// tin roofs, corrugated Nissen stores, canvas army tents, a timber range tower,
// covered firing points, earth berms, target frames and steel plates, sandbag
// and HESCO revetments, an obstacle course, a plywood shoot house and a motor
// pool. Pure kit: definitions only. tg_ namespace; metres; base z=0; front=-y.
// Door and window openings are real voids; every walk-in door is on the front.
// Surfaces go through tg_m_*: porous materials are diffuse with a low-contrast
// print, only paint, steel, glass and rubber shine.

use <gk_texgraph.scad>
use <kit_coldwar.scad>

// ================= PRNG / palette =================
function tg_sq(x) = (x*x + x*409 + 53) % 65521;
function tg_rnd(s,n) = tg_sq(tg_sq(((s % 65521)+65521)%65521)+17) % n;
function tg_rf(s,a,b) = a + (b-a)*tg_rnd(s,1000)/999;

function tg_brick(s) = [[0.42,0.40,0.35],[0.40,0.39,0.35],[0.43,0.41,0.36]][tg_rnd(s,3)];   // silicate brick
function tg_redbrick(s) = [[0.36,0.17,0.12],[0.33,0.16,0.11],[0.39,0.20,0.14]][tg_rnd(s,3)];
function tg_render(s) = [[0.38,0.36,0.27],[0.34,0.35,0.28],[0.40,0.38,0.31],[0.37,0.33,0.25]][tg_rnd(s,4)];
function tg_green(s) = [[0.14,0.20,0.12],[0.17,0.22,0.14],[0.12,0.17,0.11]][tg_rnd(s,3)];  // army paint
function tg_roofgreen(s) = [[0.16,0.24,0.17],[0.19,0.26,0.18],[0.14,0.21,0.16]][tg_rnd(s,3)];
function tg_concrete(s) = [[0.36,0.35,0.33],[0.33,0.33,0.31],[0.38,0.37,0.34]][tg_rnd(s,3)];
function tg_wood(s) = [[0.27,0.19,0.12],[0.24,0.17,0.11],[0.30,0.22,0.14]][tg_rnd(s,3)];
function tg_greywood(s) = [[0.29,0.27,0.23],[0.26,0.24,0.21],[0.31,0.28,0.24]][tg_rnd(s,3)];
function tg_ply(s) = [[0.40,0.32,0.21],[0.37,0.30,0.20],[0.42,0.35,0.24]][tg_rnd(s,3)];
function tg_canvas(s) = [[0.25,0.25,0.15],[0.22,0.23,0.14],[0.28,0.26,0.17]][tg_rnd(s,3)];
function tg_sandbag(s) = [[0.33,0.30,0.21],[0.31,0.28,0.20],[0.35,0.31,0.22]][tg_rnd(s,3)];
function tg_earth() = [0.27,0.22,0.15];
function tg_steel() = [0.22,0.23,0.22];
function tg_red() = [0.42,0.06,0.04];
function tg_white() = [0.47,0.47,0.45];
function tg_yellow() = [0.47,0.36,0.06];
function tg_black() = [0.05,0.05,0.05];
function tg_felt() = [0.38,0.35,0.26];   // khaki cloth: items read against it
function tg_paper() = [0.46,0.44,0.38];

// ================= PBR surfaces =================
module tg_m_brick(c) { gk_material(c, roughness=1) gk_texture("brick", scale=1.2) children(); }
module tg_m_render(c) { gk_material(c, roughness=1) gk_texture("plaster", scale=3.0) children(); }
module tg_m_concrete(c) { gk_material(c, roughness=1) gk_texture("concrete", scale=3.0) children(); }
module tg_m_wood(c) { gk_material(c, roughness=1) gk_texture("planks", scale=1.4) children(); }
module tg_m_timber(c) { gk_material(c, roughness=1) gk_texture("woodgrain", scale=1.6) children(); }   // posts, beams
module tg_m_ply(c) { gk_material(c, roughness=1) gk_texture("woodgrain", scale=2.4, seed=3) children(); }
module tg_m_canvas(c) { gk_material(c, roughness=1) gk_texture("canvas", scale=0.8) children(); }
module tg_m_camo() { gk_material([0.80,0.80,0.80], roughness=1) gk_texture("woodland", scale=3.5) children(); }
module tg_m_felt(c=tg_felt()) { gk_material(c, roughness=1) gk_texture("twill", scale=0.25) children(); }
module tg_m_sandbag(c) { gk_material(c, roughness=1) gk_texture("canvas", scale=0.35, seed=2) children(); }
module tg_m_earth(c=tg_earth()) { gk_material(c, roughness=1) gk_texture("dirt", scale=4.0) children(); }
module tg_m_gravel(c) { gk_material(c, roughness=1) gk_texture("gravel", scale=3.0) children(); }
module tg_m_paper(c=tg_paper()) { gk_material(c, roughness=1) children(); }
module tg_m_paint(c) { gk_material(c, roughness=0.55) children(); }            // doors, signs, frames
module tg_m_gloss(c) { gk_material(c, roughness=0.32) children(); }            // enamel plates, varnish
// Painted steel worn back to bare metal at the edges.
module tg_m_steel(c=tg_green(1)) { gk_material([1,1,1], roughness=0.72, metalness=0.9) gk_texture("chipped", palette=[c,[0.30,0.30,0.29]], scale=0.6) children(); }
module tg_m_iron(c=tg_steel()) { gk_material([1,1,1], roughness=0.85, metalness=0.8) gk_texture("rust", palette=[c,[0.20,0.075,0.03],[0.08,0.035,0.02]], scale=0.9) children(); }
// Painted corrugated sheet: the print's palette is absolute (paint, rust), so the material is white.
module tg_m_tin(c) { gk_material([1,1,1], roughness=0.62, metalness=0.7) gk_texture(tex_corrugated(24,0.08), palette=[c,[0.22,0.09,0.04]], scale=1.6) children(); }  // green tin roofs
module tg_m_corr(c=[0.24,0.27,0.20]) { gk_material([1,1,1], roughness=0.66, metalness=0.75) gk_texture(tex_corrugated(30,0.09), palette=[c,[0.22,0.09,0.04]], scale=1.8, seed=4) children(); } // olive Nissen huts
module tg_m_zinc(c=[0.33,0.34,0.34]) { gk_material(c, roughness=0.36, metalness=0.85) children(); }
module tg_m_glass() { gk_material([0.06,0.075,0.075], roughness=0.04) children(); }
module tg_m_rubber() { gk_material([0.05,0.05,0.05], roughness=0.85) children(); }
module tg_m_target_steel() { gk_material([0.42,0.40,0.36], roughness=0.6, metalness=0.2) children(); } // white-washed plates

// ================= yard clutter =================
// Small army-camp groups that fill a compound yard; base z=0, about 4 x 4 m.
module tg_clutter(kind="crates",seed=0)
{
    if(kind=="crates")
    {
        cw_prop_pallet(seed); translate([0,0,0.15]) tg_crate(seed,1.1,0.8,0.6); translate([0,0,0.75]) tg_crate(seed+1,1.0,0.6,0.45);
        translate([1.4,0.2,0]) { cw_prop_pallet(seed+2); translate([0,0,0.15]) for(i=[0:3]) translate([(i%2-0.5)*0.36,(floor(i/2)-0.5)*0.2,0]) tg_ammo_box(seed+i); }
        translate([-1.2,0.9,0]) rotate([0,0,20]) tg_crate(seed+3,1.4,0.5,0.4);
    }
    else if(kind=="drums")
    {
        for(i=[0:4]) tg_m_steel(i%3==0 ? [0.32,0.10,0.07] : tg_green(seed+i)) translate([(i%3)*0.66,floor(i/3)*0.66,0]) cylinder(h=0.9,r=0.29,$fn=12);
        translate([0.4,1.5,0.29]) rotate([0,90,25]) tg_m_steel(tg_green(seed)) cylinder(h=0.9,r=0.29,center=true,$fn=12);
        for(i=[0:2]) translate([-0.8,0.2+i*0.4,0]) tg_m_steel(tg_green(seed+i)) translate([0,0,0.24]) tg_box([0.17,0.35,0.48]);
    }
    else if(kind=="sandbags")
    {
        tg_sandbag_row(seed,3.6,4);
        for(sx=[-1,1]) translate([sx*1.9,0.9,0]) rotate([0,0,90]) tg_sandbag_row(seed+sx,2.0,3);
        translate([0,1.0,0]) tg_ammo_box(seed);
    }
    else if(kind=="tent")
        tg_tent(seed,6,4,1.6,1.1);
    else if(kind=="truck") cw_veh_truck_canvas(seed);
    else if(kind=="uaz") cw_veh_uaz_van(seed);
    else if(kind=="net")
    {
        tg_camo_net(seed,6,5,2.6);
        tg_clutter("crates",seed+5);
    }
    else if(kind=="lamp") cw_prop_lamp();
    else if(kind=="firepit")
    {
        cw_prop_campfire(seed);
        for(a=[0,120,240]) rotate([0,0,a]) translate([0,1.5,0.18]) tg_m_timber(tg_wood(seed+a)) rotate([0,90,0]) cylinder(h=1.6,r=0.17,center=true,$fn=7);
    }
    else if(kind=="woodpile") { cw_prop_woodpile(seed); translate([1.6,0,0]) cw_prop_woodpile(seed+1); }
    else if(kind=="hesco") tg_hesco(seed,4);
    else if(kind=="cable")
    {
        tg_m_timber(tg_wood(seed)) for(i=[0:1]) translate([i*1.4,0,0.6]) rotate([90,0,0]) difference() { cylinder(h=0.7,r=0.6,center=true,$fn=14); cylinder(h=0.4,r=0.62,center=true,$fn=14); }
        tg_m_rubber() for(i=[0:1]) translate([i*1.4,0,0.6]) rotate([90,0,0]) cylinder(h=0.42,r=0.45,center=true,$fn=14);
    }
    else if(kind=="trash")
    {
        tg_m_iron() cylinder(h=0.9,r=0.29,$fn=12);
        cw_prop_debris(seed);
        translate([1,0.6,0]) cw_prop_tires(seed);
    }
}

// ================= primitives =================
module tg_box(s) cube(s,center=true);
module tg_slab(L=4,D=4,t=0.2) translate([0,0,t/2]) cube([L,D,t],center=true);
// Square beam between two points (braces, rails, ladders).
module tg_beam(a,b,w=0.12) hull() { translate(a) cube(w,center=true); translate(b) cube(w,center=true); }
module tg_rod(a,b,r=0.03) hull() { translate(a) sphere(r=r,$fn=6); translate(b) sphere(r=r,$fn=6); }
module tg_post(h=2,w=0.12) translate([0,0,h/2]) cube([w,w,h],center=true);
// Gable end (triangle in the x-z plane, thickness along y) of a roof of span L and rise h.
module tg_gable(L,h,t=0.2)
    rotate([90,0,0]) linear_extrude(height=t,center=true) polygon([[-L/2,0],[L/2,0],[0,h]]);

// ================= walls with openings =================
// Wall along x, thickness along y, base z=0. holes = [[x, sill, width, height], ...].
module tg_wall(len=6, h=3, t=0.3, holes=[])
    difference()
    {
        translate([0,0,h/2]) tg_box([len,t,h]);
        for(o=holes) translate([o[0],0,o[1]+o[3]/2]) tg_box([o[2],t+0.3,o[3]]);
    }

// Window in a wall face at y=0 (outside -y): frame, glazing by variant, sill.
// v: 0 intact, 1 one pane smashed, 2 boarded, 3 bare frame, 4 barred.
module tg_window(seed=0,w=1.2,h=1.3,t=0.3,v=0)
{
    tg_m_paint(tg_green(seed)) translate([0,0,0])
    {
        for(sx=[-1,1]) translate([sx*(w/2-0.03),0,0]) tg_box([0.06,0.1,h]);
        for(sz=[-1,1]) translate([0,0,sz*(h/2-0.03)]) tg_box([w,0.1,0.06]);
        if(v!=3) { tg_box([0.05,0.08,h]); translate([0,0,h*0.2]) tg_box([w,0.08,0.05]); }
    }
    tg_m_concrete(tg_concrete(seed)) translate([0,-t/2-0.04,-h/2-0.04]) tg_box([w+0.2,0.16,0.07]);
    if(v==0) tg_m_glass() tg_box([w-0.1,0.015,h-0.1]);
    else if(v==1) tg_m_glass() { translate([w/4,0,0]) tg_box([w/2-0.08,0.015,h-0.1]); translate([-w/4,0,-h/4]) tg_box([w/2-0.08,0.015,h/2-0.1]); }
    else if(v==2) tg_m_wood(tg_greywood(seed)) for(i=[0:3]) translate([0,-0.08,-h/2+0.18+i*h/4]) rotate([0,(i%2==0?6:-5),0]) tg_box([w+0.2,0.03,0.2]);
    else if(v==5)
        tg_m_iron() { for(i=[-2:2]) translate([i*w/5,-t/2-0.02,0]) tg_box([0.025,0.025,h]); for(sz=[-1,1]) translate([0,-t/2-0.02,sz*h/3]) tg_box([w,0.025,0.03]); }
    else if(v==4)
    {
        tg_m_glass() tg_box([w-0.1,0.015,h-0.1]);
        tg_m_iron() for(i=[-2:2]) translate([i*w/5,-t/2-0.02,0]) tg_box([0.025,0.025,h]);
        tg_m_iron() for(sz=[-1,1]) translate([0,-t/2-0.02,sz*h/3]) tg_box([w,0.025,0.03]);
    }
}

// Door frame + leaf at a front opening (y=0 wall face), leaf swung open inward.
module tg_door(seed=0,w=1.0,h=2.1,t=0.3,steel=false,open=70)
{
    tg_m_paint(tg_green(seed+3))
    {
        for(sx=[-1,1]) translate([sx*(w/2+0.04),0,h/2]) tg_box([0.08,t+0.04,h]);
        translate([0,0,h+0.04]) tg_box([w+0.16,t+0.04,0.08]);
    }
    translate([w/2-0.02,t/2,0]) rotate([0,0,open]) translate([-w/2+0.04,0.03,h/2-0.01])
        if(steel) tg_m_steel(tg_green(seed+5)) tg_box([w-0.08,0.05,h-0.04]);
        else tg_m_paint(tg_green(seed+5)) { tg_box([w-0.08,0.045,h-0.04]); translate([w*0.35,-0.05,0]) tg_m_zinc() tg_box([0.12,0.03,0.03]); }
    tg_m_concrete(tg_concrete(seed)) translate([0,-t/2-0.3,0]) tg_slab(w+0.6,0.6,0.12);
}

// Plate with stencilled text (signs, display labels). Face toward -y.
module tg_plate(msg="",w=0.6,h=0.2,size=0.08,bg=tg_white(),fg=tg_black(),t=0.02)
{
    tg_m_gloss(bg) tg_box([w,t,h]);
    if(msg!="") tg_m_paint(fg) translate([0,-t/2-0.004,0]) rotate([90,0,0])
        linear_extrude(height=0.006,center=true) text(msg, size=size, halign="center", valign="center");
}

// ================= shells =================
module tg_skin(wall="brick",c=[0.4,0.4,0.4])
{
    if(wall=="render") tg_m_render(c) children();
    else if(wall=="concrete") tg_m_concrete(c) children();
    else tg_m_brick(c) children();
}

// Rectangular single-storey shell: walls with real voids, floor, inner skin
// and roof. front door at x=0 on -y. wins = window x-positions on front/back,
// side windows along y. roof: 0 flat parapet, 1 gable tin, 2 shed tin.
module tg_shell(seed=0,L=12,D=8,H=3.2,wall="brick",roof=1,wins=[],swins=[],door=1.2,backdoor=false,steeldoor=false,wv=0,c=undef,sky=[])
{
    t=0.32;
    wc = c==undef ? (wall=="render" ? tg_render(seed) : wall=="redbrick" ? tg_redbrick(seed) : tg_brick(seed)) : c;
    wz=0.95; wh=1.35; ww=1.3;
    tg_skin(wall,wc)
    {
        translate([0,-D/2+t/2,0]) tg_wall(L,H,t,concat([[0,0.12,door,2.15]],[for(x=wins) [x,wz,ww,wh]]));
        translate([0,D/2-t/2,0]) tg_wall(L,H,t,concat(backdoor?[[0,0.12,door,2.15]]:[],[for(x=wins) [x,wz,ww,wh]]));
        for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,90]) tg_wall(D-2*t+0.01,H,t,[for(y=swins) [y,wz,ww,wh]]);
    }
    // plinth: tarred base course
    tg_m_concrete([0.20,0.20,0.19]) difference()
    {
        translate([0,0,0.2]) tg_box([L+0.06,D+0.06,0.4]);
        translate([0,0,0.2]) tg_box([L-0.2,D-0.2,0.5]);
        translate([0,-D/2,0.3]) tg_box([door,1,0.7]);
        if(backdoor) translate([0,D/2,0.3]) tg_box([door,1,0.7]);
    }
    // floor + inner skin: lime wash over a green oil-paint dado
    tg_m_concrete([0.30,0.29,0.27]) tg_slab(L-2*t+0.02,D-2*t+0.02,0.12);
    tg_m_render([0.44,0.44,0.40]) for(sx=[-1,1]) translate([sx*(L/2-t-0.005),0,1.5+0.12]) tg_box([0.01,D-2*t,H-1.5-0.12]);
    tg_m_paint([0.15,0.22,0.16]) for(sx=[-1,1]) translate([sx*(L/2-t-0.006),0,0.12+0.7]) tg_box([0.012,D-2*t,1.4]);
    tg_m_paint([0.15,0.22,0.16]) translate([L/4+door/4,D/2-t-0.006,0.12+0.7]) tg_box([L/2-t-door/2,0.012,1.4]);
    tg_m_paint([0.15,0.22,0.16]) translate([-L/4-door/4,D/2-t-0.006,0.12+0.7]) tg_box([L/2-t-door/2,0.012,1.4]);
    // windows and doors
    for(x=wins) for(sy=[-1,1]) translate([x,sy*(D/2-t/2),wz+wh/2]) rotate([0,0,sy<0?0:180]) tg_window(seed+x*7+sy,ww,wh,t,wv==-1 ? tg_rnd(seed+x*13+sy,5) : wv);
    for(y=swins) for(sx=[-1,1]) translate([sx*(L/2-t/2),y,wz+wh/2]) rotate([0,0,sx<0?-90:90]) tg_window(seed+y*5+sx,ww,wh,t,wv==-1 ? tg_rnd(seed+y*17+sx,5) : wv);
    translate([0,-D/2+t/2,0.12]) tg_door(seed,door,2.1,t,steeldoor);
    if(backdoor) translate([0,D/2-t/2,0.12]) rotate([0,0,180]) tg_door(seed+1,door,2.1,t,steeldoor,60);
    // roof
    if(roof==0)
    {
        // flat slab; sky = [[x,y,w,d],...] open roof lights with a curb and a grille
        tg_m_concrete(tg_concrete(seed)) translate([0,0,H]) difference() { tg_slab(L+0.3,D+0.3,0.22); for(k=sky) translate([k[0],k[1],-0.1]) tg_slab(k[2],k[3],0.5); }
        tg_m_concrete(tg_concrete(seed+1)) for(k=sky) translate([k[0],k[1],H+0.22]) difference() { tg_slab(k[2]+0.3,k[3]+0.3,0.35); translate([0,0,-0.1]) tg_slab(k[2],k[3],0.6); }
        tg_m_iron() for(k=sky) for(i=[-2:2]) translate([k[0]+i*k[2]/5,k[1],H+0.5]) tg_box([0.03,k[3],0.03]);
        tg_m_concrete([0.14,0.14,0.13]) translate([0,0,H+0.22]) difference() { tg_slab(L+0.3,D+0.3,0.45); translate([0,0,-0.1]) tg_slab(L-0.1,D-0.1,0.7); }
        tg_m_rubber() translate([0,0,H+0.22]) tg_slab(L-0.1,D-0.1,0.02);
    }
    else if(roof==1)
    {
        rise=D*0.28; ov=0.45; slope=atan2(rise,D/2); rl=sqrt(rise*rise+D*D/4)+ov;
        tg_skin(wall,wc) for(sx=[-1,1]) translate([sx*(L/2-t/2),0,H]) rotate([0,0,90]) tg_gable(D,rise,t);
        tg_m_tin(tg_roofgreen(seed)) for(sy=[-1,1]) translate([0,sy*(D/2+ov*cos(slope))/2,H+(rise-ov*sin(slope))/2+0.03]) rotate([-sy*slope,0,0]) tg_box([L+0.7,rl,0.06]);
        tg_m_zinc() translate([0,0,H+rise+0.02]) rotate([45,0,0]) tg_box([L+0.75,0.22,0.22]);
        // rafters + ceiling joists visible inside
        tg_m_timber(tg_wood(seed)) for(i=[-2:2]) translate([i*L/5,0,H-0.08]) tg_box([0.1,D-2*t,0.16]);
        // gutters
        tg_m_zinc() for(sy=[-1,1]) translate([0,sy*(D/2+ov*0.9),H-0.1]) rotate([0,90,0]) cylinder(h=L+0.7,r=0.07,center=true,$fn=6);
        tg_m_zinc() translate([L/2+0.2,-D/2-ov*0.9,H/2]) cylinder(h=H,r=0.05,center=true,$fn=6);
    }
    else
    {
        tg_m_tin(tg_roofgreen(seed)) translate([0,0,H+0.38]) rotate([atan2(0.7,D),0,0]) tg_box([L+0.6,D+0.9,0.06]);
        tg_skin(wall,wc) for(sx=[-1,1]) translate([sx*(L/2-t/2),0,H]) rotate([90,0,90]) linear_extrude(height=t,center=true) polygon([[-D/2,0],[D/2,0],[D/2,0.7]]);
    }
}

// Corrugated Nissen hut (stores): semicircular hull, brick end walls, a door
// on the front end, windows in the gable. Open interior L x D.
module tg_bldg_nissen(seed=0,L=16,D=8)
{
    r=D/2;
    tg_m_corr() difference()
    {
        rotate([90,0,90]) cylinder(h=L,r=r,center=true,$fn=24);
        rotate([90,0,90]) cylinder(h=L+1,r=r-0.08,center=true,$fn=24);
        translate([0,0,-r]) tg_box([L+2,D+2,2*r]);
        // open roof lights along the crown
        for(i=[-2:2]) translate([i*L/5.5,0,r]) tg_box([1.8,r*0.7,1.2]);
    }
    tg_m_steel(tg_steel()) for(i=[-2:2]) for(sy=[-1,1]) translate([i*L/5.5,sy*r*0.36,r*0.93]) tg_box([2.0,0.08,0.1]);
    // ribs
    tg_m_steel(tg_steel()) for(i=[-3:3]) translate([i*L/7,0,0]) rotate([90,0,90]) difference()
    {
        cylinder(h=0.12,r=r+0.05,center=true,$fn=24);
        cylinder(h=0.2,r=r-0.02,center=true,$fn=24);
        translate([0,-r,0]) tg_box([2*r+1,2*r,1]);
    }
    // end walls with real voids
    tg_m_brick(tg_redbrick(seed)) for(sy=[-1,1]) translate([sy*(L/2-0.15),0,0]) difference()
    {
        intersection() { rotate([90,0,90]) cylinder(h=0.3,r=r-0.02,center=true,$fn=24); translate([0,0,r/2]) tg_box([1,D,r]); }
        translate([0,0,1.1]) tg_box([1,2.2,2.25]);
        for(sx=[-1,1]) translate([0,sx*r*0.55,1.9]) tg_box([1,0.9,0.8]);
    }
    for(sy=[-1,1]) translate([sy*(L/2-0.15),0,0.12]) rotate([0,0,sy<0?-90:90]) tg_door(seed+sy,2.2,2.1,0.3,true,80);
    for(sy=[-1,1]) for(sx=[-1,1]) translate([sy*(L/2-0.15),sx*r*0.55,1.9]) rotate([0,0,sy<0?-90:90]) tg_window(seed+sx+sy*3,0.9,0.8,0.3,5);
    tg_m_concrete([0.30,0.29,0.27]) tg_slab(L,D-0.2,0.12);
    tg_m_concrete(tg_concrete(seed)) for(sy=[-1,1]) translate([sy*(L/2+0.5),0,0]) tg_slab(1.2,3.2,0.12);
    // hanging lamps
    for(i=[-1:1]) translate([i*L/3,0,r-0.6]) { tg_m_iron() cylinder(h=0.6,r=0.01,$fn=4); tg_m_zinc([0.20,0.25,0.18]) cylinder(h=0.15,r1=0.25,r2=0.06,$fn=8); }
}

// Army canvas tent (UST-56 style): walls 1.8 m, ridge roof, front and back
// doorway flaps rolled up, timber floor. Interior L x D.
module tg_tent(seed=0,L=10,D=5.5,wallH=1.8,rise=1.3,open=false)
{
    c=tg_canvas(seed);
    tg_m_canvas(c)
    {
        // side walls, or with open=true rolled up under a short valance
        if(open) for(sy=[-1,1]) { translate([0,sy*(D/2),wallH-0.2]) tg_box([L,0.04,0.4]); translate([0,sy*(D/2+0.06),wallH-0.5]) rotate([0,90,0]) cylinder(h=L-0.4,r=0.11,center=true,$fn=8); }
        else for(sy=[-1,1]) translate([0,sy*(D/2),wallH/2]) tg_box([L,0.04,wallH]);
        difference()
        {
            for(sx=[-1,1]) translate([sx*L/2,0,0]) union() { translate([0,0,wallH/2]) tg_box([0.04,D,wallH]); translate([0,0,wallH]) rotate([0,0,90]) tg_gable(D,rise,0.04); }
            for(sx=[-1,1]) translate([sx*L/2,0,1.15]) tg_box([0.4,1.7,2.3]);
        }
        slope=atan2(rise,D/2); rl=sqrt(rise*rise+D*D/4)+0.35;
        for(sy=[-1,1]) translate([0,sy*D/4,wallH+rise/2]) rotate([-sy*slope,0,0]) tg_box([L+0.3,rl,0.04]);
        // rolled flaps above the doors
        for(sx=[-1,1]) translate([sx*(L/2+0.1),0,2.42]) rotate([90,0,0]) cylinder(h=1.7,r=0.09,center=true,$fn=8);
    }
    // ridge poles every ~7 m; the end poles flank the doorway instead of blocking it
    tg_m_timber(tg_wood(seed))
    {
        n=max(1,round(L/7));
        for(i=[1:n-1]) translate([-L/2+i*L/n,0,0]) tg_post(wallH+rise,0.1);
        for(sx=[-1,1]) for(sy=[-1,1]) translate([sx*(L/2-0.05),sy*0.95,0]) tg_post(wallH+rise*(1-0.95/(D/2)),0.1);
        translate([0,0,wallH+rise-0.05]) tg_box([L,0.1,0.1]);
    }
    if(open) tg_m_timber(tg_wood(seed)) for(i=[-2:2]) for(sy=[-1,1]) translate([i*L/4.2,sy*D/2,0]) tg_post(wallH,0.08);
    tg_m_wood(tg_greywood(seed)) translate([0,0,0]) tg_slab(L-0.1,D-0.1,0.08);
    // guy ropes and pegs
    tg_m_paint([0.30,0.27,0.18]) for(sy=[-1,1]) for(i=[-2:2]) tg_rod([i*L/4.2,sy*D/2,wallH],[i*L/4.2,sy*(D/2+1.4),0.05],0.012);
    tg_m_timber(tg_wood(seed+1)) for(sy=[-1,1]) for(i=[-2:2]) translate([i*L/4.2,sy*(D/2+1.4),0.1]) tg_box([0.05,0.05,0.25]);
    // stove pipe
    tg_m_iron() translate([L/2-1.6,D/4,wallH+rise/2]) cylinder(h=rise+0.6,r=0.08,$fn=8);
}

// Open-sided display pavilion: steel posts, a tin shed roof falling to the
// front (-y), a half-height corrugated back wall, sandbag ends, concrete floor.
module tg_pavilion(seed=0,L=20,D=9,h=3.2)
{
    tg_m_concrete(tg_concrete(seed)) tg_slab(L,D,0.12);
    // five bays: no post on the centre line, where visitors walk in
    tg_m_steel(tg_green(seed)) for(i=[0:5]) for(sy=[-1,1]) translate([-L/2+0.2+i*(L-0.4)/5,sy*(D/2-0.2),0]) tg_post(h+(sy>0 ? 0.5 : 0),0.16);
    tg_m_steel(tg_green(seed)) for(sy=[-1,1]) translate([0,sy*(D/2-0.2),h+(sy>0 ? 0.5 : 0)]) tg_box([L,0.16,0.2]);
    tg_m_tin(tg_roofgreen(seed)) translate([0,0,h+0.35]) rotate([atan2(0.5,D),0,0]) tg_box([L+0.8,D+1.0,0.06]);
    tg_m_corr() translate([0,D/2-0.1,1.1]) tg_box([L-0.4,0.05,2.0]);
    for(sx=[-1,1]) translate([sx*(L/2-0.35),0.4,0.12]) rotate([0,0,90]) tg_sandbag_row(seed+sx,D-2.4,4);
}

// Camouflage net on poles over a footprint (motor pool, firing line).
module tg_camo_net(seed=0,L=10,D=8,h=3.2)
{
    tg_m_timber(tg_greywood(seed)) for(sx=[-1,1]) for(sy=[-1,1]) translate([sx*(L/2-0.3),sy*(D/2-0.3),0]) tg_post(h,0.1);
    tg_m_camo() for(i=[0:3]) let(a=i*90+tg_rnd(seed+i,30)) translate([tg_rf(seed+i,-1,1),tg_rf(seed+i*3,-1,1),h-0.1+i*0.05]) rotate([tg_rf(seed+i*5,-3,3),tg_rf(seed+i*7,-3,3),a]) tg_box([L*0.8,D*0.7,0.03]);
    tg_m_camo() translate([0,0,h]) tg_box([L,D,0.03]);
}

// ================= buildings =================
// Gate guardhouse: a brick hut with wide windows and a flat roof.
module tg_bldg_guardhouse(seed=0,L=4.2,D=3.6)
    tg_shell(seed,L,D,2.8,"render",0,[],[0],0.95,false,false,0,tg_render(seed));

// Range HQ / command: long brick building, gable tin roof, porch canopy.
module tg_bldg_hq(seed=0,L=20,D=10)
{
    tg_shell(seed,L,D,3.4,"brick",1,[-L*0.32,-L*0.16,L*0.16,L*0.32],[-D*0.2,D*0.2],1.4,true,false,0);
    tg_m_steel(tg_green(seed)) for(sx=[-1,1]) translate([sx*1.6,-D/2-1.6,0]) tg_post(2.8,0.1);
    tg_m_tin(tg_roofgreen(seed+1)) translate([0,-D/2-1.0,2.9]) rotate([8,0,0]) tg_box([4.0,2.4,0.05]);
    translate([0,-D/2-0.02,2.95]) rotate([0,0,0]) tg_plate("RANGE CONTROL",3.2,0.5,0.28,tg_red(),tg_white(),0.04);
    // radio mast on the roof end
    tg_m_steel([0.32,0.32,0.30]) translate([L/2-1,D/2-1,3.4]) cylinder(h=6,r=0.06,$fn=6);
    tg_m_iron() for(i=[0:2]) tg_rod([L/2-1,D/2-1,9.2],[L/2-1+[3,-2,0][i],D/2-1+[0,2,-3][i],3.6],0.008);
}

// Barracks: long gable building with entrances at both ends of the front.
module tg_bldg_barracks(seed=0,L=24,D=9)
    tg_shell(seed,L,D,3.1,"render",1,[for(i=[-4:4]) if(i!=0) i*L/10],[0],1.4,true,false,-1);

// Armory: blockhouse with barred windows, steel doors, flat roof and an
// alarm bell. Interior is kept clear for racks and display tables.
module tg_bldg_armory(seed=0,L=16,D=10)
{
    // Display hall: open barred windows on every side and roof lights over the tables.
    tg_shell(seed,L,D,3.4,"concrete",0,[-L*0.36,-L*0.18,L*0.18,L*0.36],[-D*0.25,D*0.25],2.0,false,true,5,tg_concrete(seed),
             [[-L*0.28,0.6,1.8,1.8],[0,-0.6,1.8,1.8],[L*0.28,0.6,1.8,1.8],[-L*0.28,-3,1.4,1.4],[L*0.28,-3,1.4,1.4]]);
    tg_m_paint(tg_red()) translate([2.6,-D/2-0.05,2.7]) rotate([90,0,0]) cylinder(h=0.12,r=0.18,$fn=10);
    translate([0,-D/2-0.03,2.75]) tg_plate("ARMOURY",3.0,0.4,0.24,tg_yellow(),tg_black(),0.04);
    // floodlight over the door
    tg_m_steel([0.15,0.15,0.15]) translate([-1.5,-D/2-0.25,3.1]) rotate([30,0,0]) cylinder(h=0.3,r=0.15,$fn=8);
}

// Timber range tower: four legs, braced, glazed cabin with a balcony, stairs
// on the -x side, red range flag on the roof. Cabin floor at 4 m.
module tg_bldg_range_tower(seed=0)
{
    fl=4.0; cab=4.0;
    tg_m_timber(tg_wood(seed))
    {
        for(sx=[-1,1]) for(sy=[-1,1]) translate([sx*1.7,sy*1.7,0]) tg_post(fl+2.6,0.22);
        for(sx=[-1,1]) { tg_beam([sx*1.7,-1.7,0.4],[sx*1.7,1.7,fl-0.3]); tg_beam([sx*1.7,1.7,0.4],[sx*1.7,-1.7,fl-0.3]); }
        tg_beam([-1.7,1.7,0.4],[1.7,1.7,fl-0.3]); tg_beam([1.7,1.7,0.4],[-1.7,1.7,fl-0.3]);
    }
    tg_m_wood(tg_greywood(seed)) translate([0,0,fl]) tg_slab(cab+1.6,cab+1.6,0.15);
    // cabin
    translate([0,0,fl+0.15]) tg_shell(seed+5,cab,cab,2.4,"render",2,[0],[0],0.9,false,false,0,tg_greywood(seed));
    // balcony rail
    tg_m_timber(tg_wood(seed+1)) for(r=[0,90,180,270]) rotate([0,0,r]) translate([0,-(cab+1.5)/2,fl+1.1]) { tg_box([cab+1.6,0.06,0.06]); for(i=[-2:2]) translate([i*(cab+1.4)/4,0,-0.5]) tg_box([0.06,0.06,1.0]); }
    // stairs up the -x side (walkable ramp of treads)
    for(i=[0:15]) tg_m_wood(tg_greywood(seed+i)) translate([-2.9,-3.2+i*0.42,0.13+i*fl/16]) tg_box([1.0,0.4,0.06]);
    tg_m_timber(tg_wood(seed+2)) for(sx=[-1,1]) tg_beam([-2.9+sx*0.52,-3.4,0],[-2.9+sx*0.52,3.5,fl+0.1],0.1);
    tg_m_timber(tg_wood(seed+2)) for(sx=[-1,1]) tg_beam([-2.9+sx*0.55,-3.4,1.0],[-2.9+sx*0.55,3.5,fl+1.1],0.06);
    tg_m_wood(tg_greywood(seed)) translate([-2.9,3.5,fl]) tg_slab(1.2,1.0,0.15);
    // red range flag (raised while firing)
    tg_m_steel([0.30,0.30,0.30]) translate([1.5,1.5,fl+2.6]) cylinder(h=4.5,r=0.04,$fn=6);
    tg_m_canvas(tg_red()) translate([1.5+0.55,1.5,fl+6.6]) rotate([0,0,8]) tg_box([1.1,0.02,0.7]);
    // loudspeakers
    tg_m_steel([0.25,0.25,0.22]) for(sx=[-1,1]) translate([sx*0.8,-(cab+1.6)/2,fl+3.0]) rotate([60,0,sx*20]) cylinder(h=0.4,r1=0.08,r2=0.22,$fn=8);
}

// Covered firing point: a long tin roof on posts over the firing line,
// timber floor, sandbag rests. n lanes of width w along x; shooters face +y.
module tg_firing_shed(seed=0,n=8,w=4.0,D=5)
{
    L=n*w;
    tg_m_concrete(tg_concrete(seed)) tg_slab(L+1,D,0.12);
    tg_m_timber(tg_wood(seed)) for(i=[0:n]) for(sy=[-1,1]) translate([-L/2+i*w,sy*(D/2-0.2),0]) tg_post(sy<0 ? 3.0 : 2.6,0.18);
    tg_m_timber(tg_wood(seed)) for(sy=[-1,1]) translate([0,sy*(D/2-0.2),sy<0?2.95:2.55]) tg_box([L+0.4,0.18,0.18]);
    tg_m_tin(tg_roofgreen(seed)) translate([0,0,2.85]) rotate([-atan2(0.4,D),0,0]) tg_box([L+1.0,D+1.0,0.05]);
    // baffles: sandbag rests along the front edge, lane numbers
    for(i=[0:n-1]) translate([-L/2+(i+0.5)*w,D/2-0.6,0.12])
    {
        tg_sandbag_row(seed+i,1.6,2);
        translate([0,0.8,2.3]) tg_plate(str(i+1),0.5,0.5,0.32,tg_white(),tg_black(),0.04);
        // lane divider
        if(i>0) translate([-w/2,-1.2,0]) tg_m_ply(tg_ply(seed+i)) translate([0,0,1.0]) tg_box([0.05,2.2,2.0]);
    }
}

// Shooting bench: heavy timber table and seat, for zeroing scopes.
module tg_shooting_bench(seed=0)
{
    tg_m_timber(tg_wood(seed))
    {
        translate([0,0,0.86]) tg_box([1.4,0.8,0.08]);
        for(sx=[-1,1]) for(sy=[-1,1]) translate([sx*0.6,sy*0.32,0]) tg_post(0.84,0.09);
        translate([0,-0.75,0.45]) tg_box([0.9,0.32,0.06]);
        for(sx=[-1,1]) translate([sx*0.35,-0.75,0]) tg_post(0.44,0.08);
    }
    tg_m_sandbag(tg_sandbag(seed)) translate([0,0.15,0.95]) scale([1,0.6,0.5]) sphere(r=0.25,$fn=8);
}

// ================= range furniture =================
// Earth berm: trapezoid bank of length L along x, height h, crest width c.
module tg_berm(L=40,h=4,c=2,b=12)
    tg_m_earth() rotate([90,0,90]) linear_extrude(height=L,center=true) polygon([[-b/2,0],[b/2,0],[c/2,h],[-c/2,h]]);

// Sandbag row: n courses along x of length L.
module tg_sandbag_row(seed=0,L=3,n=3)
{
    k=max(1,round(L/0.62));
    for(r=[0:n-1]) for(i=[0:k-1-(r%2)]) tg_m_sandbag(tg_sandbag(seed+r*7+i))
        translate([-L/2+0.31+i*L/k+(r%2)*L/k/2,0,0.11+r*0.2]) rotate([0,0,tg_rf(seed+i*3+r,-4,4)]) scale([0.31,0.21,0.11]) sphere(r=1,$fn=8);
}

// HESCO bastion: wire-mesh cage filled with earth, n cells along x.
module tg_hesco(seed=0,n=3,h=1.4,s=1.05)
{
    for(i=[0:n-1]) translate([(i-(n-1)/2)*s,0,0])
    {
        tg_m_earth([0.30,0.25,0.17]) translate([0,0,h/2]) tg_box([s-0.04,s-0.04,h]);
        tg_m_canvas([0.33,0.31,0.22]) translate([0,0,h/2]) tg_box([s,s,h-0.25]);
        tg_m_iron([0.28,0.28,0.26]) for(sx=[-1,1]) for(sy=[-1,1]) translate([sx*(s/2),sy*(s/2),0]) cylinder(h=h,r=0.012,$fn=4);
    }
}

// Paper target on a timber frame: aim point (bull) at z=zc, face toward -y.
// The nd_target node marks it for scoring; frames stand on posts.
module tg_target_frame(seed=0,kind="bull",zc=1.5)
{
    tg_m_timber(tg_greywood(seed)) for(sx=[-1,1]) translate([sx*0.55,0.05,0]) tg_post(zc+0.65,0.07);
    tg_m_timber(tg_greywood(seed)) translate([0,0.05,zc+0.6]) tg_box([1.2,0.07,0.07]);
    tg_m_ply(tg_ply(seed)) translate([0,0.04,zc]) tg_box([1.0,0.02,1.2]);
}
// Bullseye face: rings of radius r, 10..1, at the origin; face toward -y.
module tg_face_bull(r=0.4)
{
    tg_m_paper() rotate([90,0,0]) cylinder(h=0.004,r=r*1.15,center=true,$fn=24);
    for(i=[0:9]) tg_m_paper(i%2==0 ? [0.06,0.06,0.06] : tg_paper()) translate([0,-0.003-i*0.0006,0]) rotate([90,0,0]) cylinder(h=0.002,r=r*(10-i)/10,center=true,$fn=24);
    tg_m_paper(tg_red()) translate([0,-0.012,0]) rotate([90,0,0]) cylinder(h=0.002,r=r*0.05,center=true,$fn=12);
}
// Torso silhouette ("Ivan" figure) face toward -y, origin at the centre of mass.
module tg_face_figure(seed=0)
{
    tg_m_paper([0.20,0.24,0.14]) rotate([90,0,0]) linear_extrude(height=0.006,center=true)
        polygon([[-0.25,-0.55],[0.25,-0.55],[0.28,0.05],[0.2,0.18],[0.11,0.2],[0.12,0.36],[0.07,0.45],[-0.07,0.45],[-0.12,0.36],[-0.11,0.2],[-0.2,0.18],[-0.28,0.05]]);
    tg_m_paper(tg_white()) translate([0,-0.006,0]) rotate([90,0,0]) cylinder(h=0.002,r=0.11,center=true,$fn=16);
}
// Steel plate (gong) hanging on chains from a frame; origin at the plate centre.
module tg_face_plate(r=0.3,square=false)
{
    tg_m_target_steel() if(square) tg_box([2*r,0.03,2*r]); else rotate([90,0,0]) cylinder(h=0.03,r=r,center=true,$fn=16);
}
module tg_plate_stand(seed=0,zc=1.0,r=0.3)
{
    tg_m_iron() { for(sx=[-1,1]) translate([sx*(r+0.25),0.15,0]) tg_post(zc+r+0.35,0.06); translate([0,0.15,zc+r+0.32]) tg_box([2*r+0.6,0.06,0.06]); }
    tg_m_iron() for(sx=[-1,1]) tg_rod([sx*r*0.6,0.15,zc+r+0.3],[sx*r*0.6,0.0,zc+r*0.7],0.012);
    tg_m_concrete(tg_concrete(seed)) translate([0,0.15,0]) tg_slab(2*r+0.9,0.5,0.15);
}

// Distance marker post ("100") facing -y.
module tg_distance_marker(d=100)
{
    tg_m_paint(tg_white()) tg_post(1.2,0.12);
    translate([0,-0.08,1.45]) tg_plate(str(d),0.7,0.45,0.25,tg_red(),tg_white(),0.04);
    tg_m_paint(tg_red()) translate([0,0,0.6]) tg_box([0.13,0.13,0.2]);
}

// ================= display furniture =================
// Display table: trestle legs, green felt cloth. Top at z = 0.86.
function tg_table_top() = 0.86;
module tg_table_display(seed=0,L=2.4,D=1.2)
{
    tg_m_steel([0.20,0.22,0.18]) for(sx=[-1,1]) translate([sx*(L/2-0.2),0,0])
    {
        for(sy=[-1,1]) tg_beam([0,sy*(D/2-0.12),0],[0,sy*0.05,0.8],0.04);
        translate([0,0,0.4]) tg_box([0.04,D*0.6,0.04]);
    }
    tg_m_ply(tg_ply(seed)) translate([0,0,0.82]) tg_box([L,D,0.03]);
    tg_m_felt() { translate([0,0,0.84]) tg_box([L+0.04,D+0.04,0.01]); for(sy=[-1,1]) translate([0,sy*(D/2+0.02),0.72]) tg_box([L+0.04,0.008,0.25]); }
}

// Wall rifle rack: slotted stock rail and padded barrel rest for n rifles
// standing against a wall at y=0 (rack faces -y).
module tg_rifle_rack(seed=0,n=6,s=0.32)
{
    W=n*s+0.2;
    tg_m_timber(tg_wood(seed)) { translate([0,-0.18,0.1]) tg_box([W,0.3,0.2]); translate([0,-0.06,1.15]) tg_box([W,0.12,0.12]); for(sx=[-1,1]) translate([sx*W/2,-0.1,0]) tg_post(1.3,0.08); }
    tg_m_felt([0.30,0.08,0.06]) translate([0,-0.13,1.22]) tg_box([W,0.04,0.04]);
    // standing practice rifles (decor only, the real ones lie on the tables)
    for(i=[0:n-1]) translate([-W/2+0.1+(i+0.5)*s,-0.2,0.2])
    {
        tg_m_timber(tg_wood(seed+i)) rotate([-6,0,0]) translate([0,0,0.4]) tg_box([0.05,0.09,0.8]);
        tg_m_steel([0.08,0.08,0.08]) rotate([-6,0,0]) translate([0,0,1.1]) cylinder(h=0.25,r=0.012,$fn=6);
    }
}

// Steel shelving unit (stores): n shelves, faces -y. Shelf heights from tg_shelf_z.
function tg_shelf_z(i) = 0.12 + i*0.48;
module tg_shelf(seed=0,L=2.0,D=0.6,n=4)
{
    tg_m_steel([0.24,0.27,0.22]) { for(sx=[-1,1]) for(sy=[-1,1]) translate([sx*(L/2-0.03),sy*(D/2-0.03),0]) tg_post(tg_shelf_z(n-1)+0.1,0.04); for(i=[0:n-1]) translate([0,0,tg_shelf_z(i)-0.02]) tg_box([L,D,0.03]); }
}

// Wooden mannequin on a stand: a dummy wearing a draped olive smock.
module tg_mannequin(seed=0,c=tg_canvas(0))
{
    tg_m_steel([0.15,0.15,0.15]) { cylinder(h=0.04,r=0.25,$fn=10); cylinder(h=0.9,r=0.02,$fn=6); }
    tg_m_gloss([0.40,0.33,0.24]) { translate([0,0,1.25]) scale([0.22,0.13,0.36]) sphere(r=1,$fn=10); translate([0,0,1.7]) sphere(r=0.11,$fn=10); translate([0,0,1.58]) cylinder(h=0.1,r=0.05,$fn=6); }
    tg_m_canvas(c) translate([0,0,1.15]) scale([0.25,0.15,0.36]) sphere(r=1,$fn=10);
}

// Clothes rail with hangers and draped garments (quartermaster decor).
module tg_clothes_rail(seed=0,L=1.8,n=6)
{
    tg_m_steel([0.30,0.30,0.30]) { for(sx=[-1,1]) { translate([sx*L/2,0,0]) tg_post(1.6,0.035); translate([sx*L/2,0,0.02]) tg_box([0.06,0.6,0.04]); } translate([0,0,1.6]) rotate([0,90,0]) cylinder(h=L,r=0.015,center=true,$fn=6); }
    for(i=[0:n-1]) translate([-L/2+(i+0.5)*L/n,0,1.12]) rotate([0,0,90])
        gk_material(i%3==0 ? [0.80,0.80,0.80] : tg_canvas(seed+i), roughness=1) gk_texture(i%3==0 ? "woodland" : "canvas", scale=i%3==0 ? 1.2 : 0.4) tg_box([0.5,0.06,0.9]);
}

// Notice board: two legs, a roofed corkboard with pinned sheets.
module tg_notice_board(seed=0,msg="RANGE ORDERS")
{
    tg_m_timber(tg_wood(seed)) for(sx=[-1,1]) translate([sx*0.9,0.05,0]) tg_post(2.3,0.1);
    tg_m_ply(tg_ply(seed)) translate([0,0.05,1.45]) tg_box([1.9,0.04,1.1]);
    tg_m_tin(tg_roofgreen(seed)) translate([0,0,2.35]) rotate([-15,0,0]) tg_box([2.2,0.5,0.03]);
    for(i=[0:5]) tg_m_paper() translate([-0.7+(i%3)*0.7,0.02,1.2+floor(i/3)*0.5]) rotate([0,tg_rf(seed+i,-6,6),0]) tg_box([0.4,0.01,0.3]);
    translate([0,0.01,1.9]) tg_plate(msg,1.5,0.16,0.1,tg_red(),tg_white(),0.02);
}

// Flagpole with a pennant; h tall.
module tg_flagpole(seed=0,h=9,c=tg_red())
{
    tg_m_zinc([0.40,0.40,0.40]) cylinder(h=h,r=0.06,$fn=8);
    tg_m_concrete(tg_concrete(seed)) cylinder(h=0.3,r=0.5,$fn=8);
    tg_m_canvas(c) translate([0.7,0,h-0.6]) rotate([0,0,tg_rf(seed,-12,12)]) tg_box([1.4,0.02,0.9]);
}

// Painted kerb stone (whitewashed army style) along x.
module tg_kerb(L=4) tg_m_paint([0.44,0.44,0.42]) translate([0,0,0.1]) tg_box([L,0.2,0.2]);
// Painted rock (border of the parade ground).
module tg_white_rock(seed=0) tg_m_paint([0.44,0.44,0.42]) translate([0,0,0.1]) scale([tg_rf(seed,0.25,0.4),tg_rf(seed+1,0.2,0.35),0.18]) sphere(r=1,$fn=6);

// Red-white barrier pole on a counterweight post (gate).
module tg_barrier(seed=0,L=6,open=0)
{
    tg_m_steel(tg_green(seed)) translate([0,0,0]) tg_box([0.4,0.4,2.0]);
    tg_m_concrete(tg_concrete(seed)) tg_slab(0.8,0.8,0.15);
    translate([0,0,1.0]) rotate([0,-open,0]) for(i=[0:7]) tg_m_paint(i%2==0 ? tg_red() : tg_white()) translate([0.3+(i+0.5)*L/8,0,0]) tg_box([L/8,0.12,0.12]);
    tg_m_steel(tg_green(seed)) translate([L+0.3,0,0]) tg_post(0.95,0.12);
}

// Concrete panel fence ("plita" PO-2): panels 4 m long, 2.5 m high, with
// barbed wire on top; along x.
module tg_fence_panel(seed=0,n=1)
{
    for(i=[0:n-1]) translate([(i-(n-1)/2)*4.0,0,0])
    {
        tg_m_concrete(tg_concrete(seed+i)) { translate([0,0,1.25]) tg_box([3.95,0.16,2.5]); for(z=[0.6,1.3,2.0]) translate([0,-0.09,z]) tg_box([3.6,0.04,0.04]); }
        tg_m_concrete(tg_concrete(seed+i+3)) translate([2.0,0,0]) tg_post(2.7,0.25);
        tg_m_iron() for(sy=[-1,1]) { translate([2.0,0,2.7]) rotate([sy*30,0,0]) translate([0,0,0.25]) tg_box([0.04,0.04,0.5]); }
        tg_m_iron() for(k=[0:2]) translate([0,(k-1)*0.18,2.75+abs(k-1)*0.15]) rotate([0,90,0]) cylinder(h=4,r=0.008,center=true,$fn=4);
    }
}

// Chain-link fence along x, posts every 3 m, n bays.
module tg_fence_mesh(seed=0,n=1,h=2.2)
{
    for(i=[0:n-1]) translate([(i-(n-1)/2)*3.0,0,0])
    {
        tg_m_steel([0.26,0.27,0.26]) translate([1.5,0,0]) tg_post(h+0.2,0.06);
        gk_material([0.30,0.31,0.30], roughness=0.6, metalness=0.7) translate([0,0,h/2+0.05]) tg_box([3,0.015,h]);
    }
}

// Ammunition box (olive metal can, stencilled), a stack of crates.
module tg_ammo_box(seed=0)
{
    tg_m_steel(tg_green(seed)) translate([0,0,0.12]) tg_box([0.3,0.16,0.24]);
    tg_m_steel([0.12,0.12,0.12]) translate([0,0,0.255]) tg_box([0.12,0.03,0.03]);
    tg_m_paint(tg_yellow()) translate([0,-0.082,0.14]) tg_box([0.18,0.004,0.04]);
}
module tg_crate(seed=0,L=1.0,D=0.6,H=0.5)
{
    tg_m_timber(tg_green(seed)) translate([0,0,H/2]) tg_box([L,D,H]);
    tg_m_timber(tg_wood(seed)) for(sx=[-1,1]) translate([sx*(L/2-0.06),0,H/2]) tg_box([0.06,D+0.02,H+0.02]);
    tg_m_paint(tg_yellow()) translate([0,-D/2-0.004,H*0.6]) tg_box([L*0.5,0.004,0.06]);
}

// Floodlight mast for the parade ground / motor pool.
module tg_floodlight(seed=0,h=8)
{
    tg_m_concrete(tg_concrete(seed)) cylinder(h=h,r1=0.16,r2=0.1,$fn=8);
    tg_m_steel([0.18,0.18,0.18]) translate([0,0,h]) { tg_box([1.0,0.1,0.1]); for(sx=[-1,1]) translate([sx*0.4,-0.1,-0.05]) rotate([60,0,0]) cylinder(h=0.25,r=0.14,$fn=8); }
}

// Fuel point: a raised fuel tank on a frame plus a hand pump and drums.
module tg_fuel_point(seed=0)
{
    tg_m_steel(tg_green(seed)) { translate([0,0,2.2]) rotate([0,90,0]) cylinder(h=4,r=0.8,center=true,$fn=16); for(sx=[-1,1]) for(sy=[-1,1]) translate([sx*1.5,sy*0.5,0]) tg_post(1.6,0.12); }
    tg_m_iron() translate([2.3,-0.6,0]) { tg_post(1.1,0.18); translate([0,-0.15,0.9]) rotate([0,0,0]) tg_box([0.05,0.3,0.05]); }
    for(i=[0:3]) tg_m_steel(i%2==0 ? tg_green(seed+i) : [0.32,0.10,0.07]) translate([-2.6+(i%2)*0.65,-0.9-floor(i/2)*0.65,0]) cylinder(h=0.9,r=0.29,$fn=12);
}

// Open vehicle shed: posts, tin shed roof, back wall of corrugated sheet.
module tg_vehicle_shed(seed=0,L=18,D=8,h=4.2)
{
    tg_m_steel(tg_green(seed)) for(i=[0:3]) for(sy=[-1,1]) translate([-L/2+i*L/3,sy*(D/2-0.15),0]) tg_post(sy<0 ? h : h+0.6,0.2);
    tg_m_tin(tg_roofgreen(seed)) translate([0,0,h+0.35]) rotate([atan2(0.6,D),0,0]) tg_box([L+0.8,D+1.2,0.06]);
    tg_m_corr() translate([0,D/2,h/2+0.3]) tg_box([L,0.05,h+0.6]);
    tg_m_concrete(tg_concrete(seed)) tg_slab(L,D,0.1);
    tg_m_paint(tg_yellow()) for(i=[1:2]) translate([-L/2+i*L/3,0,0.105]) tg_box([0.12,D-1,0.004]);
}

// ================= obstacle course =================
module tg_ob_wall(seed=0,h=2.0) { tg_m_wood(tg_greywood(seed)) translate([0,0,h/2]) tg_box([3.0,0.12,h]); tg_m_timber(tg_wood(seed)) for(sx=[-1,1]) translate([sx*1.6,0,0]) tg_post(h+0.2,0.16); }
module tg_ob_beam(seed=0,L=6) { tg_m_timber(tg_wood(seed)) { translate([0,0,1.0]) tg_box([L,0.14,0.14]); for(i=[-1:1]) translate([i*L/2.2,0,0]) tg_post(1.0,0.14); } }
module tg_ob_bars(seed=0,L=5)
{
    tg_m_timber(tg_wood(seed)) for(sx=[-1,1]) for(sy=[-1,1]) translate([sx*L/2,sy*0.4,0]) tg_post(2.3,0.14);
    tg_m_steel([0.30,0.30,0.28]) for(sy=[-1,1]) translate([0,sy*0.4,2.3]) rotate([0,90,0]) cylinder(h=L,r=0.03,center=true,$fn=6);
    tg_m_steel([0.30,0.30,0.28]) for(i=[0:9]) translate([-L/2+0.25+i*(L-0.5)/9,0,2.3]) rotate([90,0,0]) cylinder(h=0.8,r=0.02,center=true,$fn=6);
}
module tg_ob_net(seed=0,L=8,W=3)
{
    tg_m_timber(tg_wood(seed)) for(sx=[-1,1]) for(sy=[-1,1]) translate([sx*L/2,sy*W/2,0]) tg_post(0.7,0.1);
    gk_material([0.20,0.20,0.14], roughness=1) for(i=[0:8]) translate([-L/2+i*L/8,0,0.62]) tg_box([0.02,W,0.02]);
    gk_material([0.20,0.20,0.14], roughness=1) for(i=[0:4]) translate([0,-W/2+i*W/4,0.62]) tg_box([L,0.02,0.02]);
}
module tg_ob_tires(seed=0,n=8) for(i=[0:n-1]) tg_m_rubber() translate([(floor(i/2)-(n/2-1)/2)*0.9,(i%2-0.5)*0.85,0.12]) rotate_extrude($fn=12) translate([0.32,0]) scale([0.12,0.12]) circle(r=1,$fn=6);
module tg_ob_ladder(seed=0,h=4)
{
    tg_m_timber(tg_wood(seed)) { for(sx=[-1,1]) translate([sx*1.2,0,0]) tg_post(h,0.16); for(i=[1:5]) translate([0,0,i*h/5.5]) rotate([0,90,0]) cylinder(h=2.4,r=0.05,center=true,$fn=6); }
}
module tg_ob_trench(seed=0,L=8)
{
    // revetted trench rim: low log walls and a plank floor (drawn above grade)
    tg_m_timber(tg_wood(seed)) for(sy=[-1,1]) translate([0,sy*0.75,0.25]) tg_box([L,0.2,0.5]);
    tg_m_sandbag(tg_sandbag(seed)) for(sy=[-1,1]) translate([0,sy*0.95,0.55]) tg_box([L,0.45,0.2]);
}

// Plywood shoot-house wall segment (no roof): L along x, door gap optional.
module tg_ply_wall(seed=0,L=4,h=2.4,gap=0)
{
    difference()
    {
        tg_m_ply(tg_ply(seed)) translate([0,0,h/2]) tg_box([L,0.05,h]);
        if(gap>0) translate([0,0,1.05]) tg_box([gap,0.2,2.1]);
    }
    tg_m_timber(tg_wood(seed)) { for(sx=[-1,1]) translate([sx*(L/2-0.05),0.06,0]) tg_post(h,0.09); translate([0,0.06,h-0.05]) tg_box([L,0.09,0.09]); }
}
