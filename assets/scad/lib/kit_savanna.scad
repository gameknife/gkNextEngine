// kit_savanna.scad — East African savanna (Maasai Mara / Serengeti edge)
// architecture, compound furniture and flora for abandoned NextDayz maps.
// Pure kit: definitions only. sv_ namespace; metres; base z=0; front=-y.
// Openings are real voids, the walk-in door sits on the front axis (x=0).
// Regional character lives in the silhouette and the materials: low concrete
// block dukas under rusting corrugated iron (mabati) with a veranda on poles
// and a painted dado, round mud huts under thatch cones, Maasai dung-plastered
// loaf huts inside thorn rings, mission halls, ranger posts with metal
// Uniports, canvas safari tents beside thatched bandas; flat-topped acacias,
// yellow fever trees and fig galleries along the river, red murram underfoot.

use <gk_texgraph.scad>
use <gk_flora.scad>

// ================= PRNG / palette =================
function sv_sq(x) = (x*x + x*409 + 53) % 65521;
function sv_rnd(s,n) = sv_sq(sv_sq(((s % 65521)+65521)%65521)+17) % n;
function sv_rf(s,a,b) = a + (b-a)*sv_rnd(s,1000)/999;

// Path tracing lifts albedo: keep everything below 0.5.
// Limewash and oil paint over blockwork: sun-bleached, never pastel-European.
function sv_wash(s) = [[0.39,0.36,0.28],[0.38,0.33,0.23],[0.36,0.37,0.32],[0.40,0.34,0.25],[0.34,0.35,0.34]][sv_rnd(s,5)];
// The dado / shopfront colour: the loud enamel paints of an upcountry duka.
function sv_enamel(s) = [[0.10,0.24,0.40],[0.12,0.33,0.17],[0.45,0.11,0.08],[0.46,0.34,0.06],[0.08,0.30,0.31],[0.40,0.20,0.07]][sv_rnd(s,6)];
function sv_mud(s) = [[0.38,0.24,0.15],[0.35,0.22,0.14],[0.41,0.28,0.17],[0.33,0.23,0.16]][sv_rnd(s,4)];
function sv_dung(s) = [[0.30,0.25,0.19],[0.27,0.22,0.17],[0.33,0.27,0.20]][sv_rnd(s,3)];
function sv_thatch(s) = [[0.36,0.30,0.18],[0.31,0.26,0.16],[0.40,0.34,0.21],[0.27,0.23,0.15]][sv_rnd(s,4)];
function sv_zinc(s) = [[0.30,0.31,0.32],[0.27,0.28,0.29],[0.25,0.26,0.27],[0.26,0.21,0.17]][sv_rnd(s,4)];
function sv_paintiron(s) = [[0.12,0.25,0.36],[0.14,0.30,0.18],[0.38,0.12,0.09],[0.30,0.31,0.30]][sv_rnd(s,4)];
function sv_pole(s) = [[0.30,0.24,0.17],[0.26,0.20,0.14],[0.34,0.28,0.20]][sv_rnd(s,3)];
function sv_canvas(s) = [[0.30,0.31,0.21],[0.36,0.33,0.24],[0.26,0.29,0.21]][sv_rnd(s,3)];
function sv_stone(s) = [[0.33,0.31,0.28],[0.37,0.33,0.28],[0.29,0.28,0.26]][sv_rnd(s,3)];
function sv_olive() = [0.20,0.24,0.14];
function sv_dark() = [0.06,0.06,0.05];

// ================= PBR surfaces =================
// Porous materials are diffuse with a low-contrast print; only enamel paint,
// glass and bare metal shine (see kit_bohemia.scad for the reasoning).
// The plaster print's second colour is its spalling; kept close to the wash
// so a wall reads as patched render, not as a spotted hide.
module sv_m_block(c) { gk_material(c, roughness=1) gk_texture("plaster", palette=[[1,1,1],[0.80,0.74,0.68]], scale=3.2, relief=0.35) children(); }      // rendered blockwork
module sv_m_mud(c) { gk_material(c, roughness=1) gk_texture("mudplaster", palette=[[1,1,1],[0.86,0.78,0.68]], scale=1.6, seed=3) children(); } // hand-smeared mud / dung plaster
module sv_m_stone(c) { gk_material(c, roughness=1) gk_texture("stone", scale=2.2) children(); }
module sv_m_concrete(c) { gk_material(c, roughness=1) gk_texture("concrete", scale=3.0) children(); }
module sv_m_thatch(c) { gk_material(c, roughness=1) gk_texture("bark", scale=1.3) children(); }         // grass thatch courses
module sv_m_wood(c) { gk_material(c, roughness=1) gk_texture("planks", scale=1.4) children(); }
module sv_m_pole(c) { gk_material(c, roughness=1) gk_texture("bark", scale=0.6) children(); }           // peeled poles, thorn wood
module sv_m_canvas(c) { gk_material(c, roughness=1) gk_texture("canvas", scale=0.8) children(); }
module sv_m_enamel(c) { gk_material(c, roughness=0.45) children(); }                                    // oil paint: dado, doors
// Corrugated iron: galvanised sheet blooming into rust. The print carries the
// colours, so the material is white.
// rust is the caller's 0..1 weathering; the print gets a third of it, or the
// blooms read as camouflage from the air.
module sv_m_mabati(c=[0.30,0.31,0.32], rust=0.3) { gk_material([1,1,1], roughness=1, metalness=0.3) gk_texture(tex_corrugated(24, 0.02+rust*0.18), palette=[c,[0.19,0.09,0.045]], scale=1.6) children(); }
module sv_m_iron(c=[0.17,0.16,0.15]) { gk_material([1,1,1], roughness=0.85, metalness=0.8) gk_texture("rust", palette=[c,[0.20,0.075,0.03],[0.08,0.035,0.02]], scale=0.9) children(); }
module sv_m_plastic(c) { gk_material(c, roughness=0.5) children(); }                                    // water tanks, jerrycans
module sv_m_glass() { gk_material([0.06,0.075,0.075], roughness=0.04, shadow=false) children(); }
module sv_mat(m,c) {
    if(m=="mud") sv_m_mud(c) children(); else if(m=="iron") sv_m_mabati(c,0.35) children();
    else if(m=="stone") sv_m_stone(c) children(); else if(m=="canvas") sv_m_canvas(c) children();
    else if(m=="wood") sv_m_wood(c) children(); else sv_m_block(c) children();
}

module sv_box(s) cube(s,center=true);
module sv_flipx(f) { if(f<0) mirror([1,0,0]) children(); else children(); }
module sv_slab(L=4,D=4,t=0.2) translate([0,0,t/2]) cube([L,D,t],center=true);
module sv_rod(a,b,r=0.05,n=6)
    let(d=b-a,l=norm(d)) translate(a) rotate([0,acos(d[2]/l),atan2(d[1],d[0])]) cylinder(h=l,r=r,$fn=n);

// ================= Parts =================
// Wall strip along x, centred on y=0, with rectangular voids. ops=[[xc,zb,w,h],...]
// sorted by xc.
module sv_part_wall(L=10,H=2.6,ops=[],t=0.3,c=[0.4,0.4,0.4],m="block")
{
    n=len(ops);
    e=concat([-L/2],[for(o=ops) each [o[0]-o[2]/2,o[0]+o[2]/2]],[L/2]);
    sv_mat(m,c)
    {
        for(i=[0:n]) if(e[2*i+1]-e[2*i]>0.01)
            translate([(e[2*i]+e[2*i+1])/2,0,H/2]) sv_box([e[2*i+1]-e[2*i],t,H]);
        for(o=ops)
        {
            if(o[1]>0.01) translate([o[0],0,o[1]/2]) sv_box([o[2],t,o[1]]);
            if(H-o[1]-o[3]>0.01) translate([o[0],0,(o[1]+o[3]+H)/2]) sv_box([o[2],t,H-o[1]-o[3]]);
        }
    }
}

// Painted dado band on the outer face (y<0 side) of a wall strip, broken by
// openings that reach the ground.
module sv_part_dado(L=10,h=1.0,ops=[],c=[0.1,0.25,0.4],t=0.3)
    translate([0,-t/2-0.006,0]) sv_m_enamel(c)
    {
        d=[for(o=ops) if(o[1]<h) o];
        e=concat([-L/2],[for(o=d) each [o[0]-o[2]/2,o[0]+o[2]/2]],[L/2]);
        for(i=[0:len(d)]) if(e[2*i+1]-e[2*i]>0.01) translate([(e[2*i]+e[2*i+1])/2,0,h/2]) sv_box([e[2*i+1]-e[2*i],0.012,h]);
        for(o=d) if(o[1]>0.01) translate([o[0],0,o[1]/2]) sv_box([o[2],0.012,o[1]]);
    }

// Window on a wall face at y=0 (outside = -y), centred on its opening.
// Variants: 0 timber shutters (one ajar), 1 burglar bars over a bare hole,
// 2 steel casement with broken glass, 3 patched with an iron sheet.
function sv_win_var(s) = sv_rnd(s,4);
module sv_part_window(seed=0,w=0.9,h=1.0)
{
    v=sv_win_var(seed);
    sv_m_enamel(sv_paintiron(seed+1)) translate([0,-0.02,0])
    {
        for(sx=[-1,1]) translate([sx*(w/2-0.025),0,0]) sv_box([0.05,0.06,h]);
        for(sz=[-1,1]) translate([0,0,sz*(h/2-0.025)]) sv_box([w,0.06,0.05]);
    }
    if(v==0) sv_m_wood(sv_pole(seed))
    {
        translate([-w/4,-0.04,0]) sv_box([w/2-0.04,0.03,h-0.08]);
        translate([w/2,-0.05,0]) rotate([0,0,-65]) translate([-w/4,0,0]) sv_box([w/2-0.04,0.03,h-0.08]);
    }
    if(v==1 || v==2) sv_m_iron() for(x=[-w/2+w/5:w/5:w/2-0.01]) translate([x,-0.03,0]) sv_box([0.02,0.02,h-0.06]);
    if(v==2) sv_m_glass() translate([-w/4,0.02,-h*0.1]) sv_box([w/2-0.08,0.01,h*0.6]);
    if(v==3) sv_m_mabati(sv_zinc(seed),0.55) translate([0.03,-0.05,0.02]) rotate([0,sv_rf(seed,-5,5),0]) sv_box([w+0.16,0.02,h+0.1]);
}

// Steel-sheet door leaf, hinge on its -x edge, swung by `open` degrees inward.
module sv_part_leaf(seed=0,w=0.9,h=2.0,open=70)
    translate([-w/2,0,0]) rotate([0,0,open]) translate([w/2,0,0])
    {
        sv_m_enamel(sv_paintiron(seed)) translate([0,0,h/2]) sv_box([w,0.035,h]);
        sv_m_iron() { translate([0,-0.025,h*0.5]) sv_box([w,0.02,0.05]); translate([w/2-0.08,-0.03,h*0.48]) sv_box([0.1,0.03,0.04]); }
    }

// Rectangular shell: four walls with voids. fops/bops over length L, sops over
// the side walls (length D-2t), all in each wall's own x.
module sv_part_shell(L=8,D=6,H=2.6,t=0.3,c=[0.4,0.4,0.4],m="block",fops=[],bops=[],sops=[],Hf=0)
{
    translate([0,-D/2+t/2,0]) sv_part_wall(L,Hf>0 ? Hf : H,fops,t,c,m);
    translate([0,D/2-t/2,0]) rotate([0,0,180]) sv_part_wall(L,H,bops,t,c,m);
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90]) sv_part_wall(D-2*t,H,sops,t,c,m);
}

// Cement screed (or swept earth) floor; top at z=0.1.
module sv_part_floor(seed=0,L=8,D=6,earth=false)
{
    if(earth) sv_m_mud(sv_mud(seed)*0.8) sv_slab(L,D,0.1);
    else
    {
        sv_m_concrete([0.34,0.33,0.31]) sv_slab(L,D,0.1);
        // Red oxide polish worn through by decades of feet.
        if(sv_rnd(seed,2)==0) gk_material([0.30,0.11,0.08],roughness=0.3) translate([0,0,0.1]) sv_box([L*0.7,D*0.6,0.006]);
    }
}

// Corrugated gable roof sitting on z=0 (eaves at the wall top), ridge along x,
// with closed gables in the wall material.
module sv_part_roof_gable(seed=0,L=8,D=6,rise=1.4,ov=0.45,c=[0.4,0.4,0.4],gc=[0.4,0.4,0.4],gm="block",t=0.3,rust=0.3)
{
    a=atan(rise/(D/2)); hs=D/2+ov; sl=hs/cos(a);
    for(sy=[-1,1]) sv_m_mabati(c,rust) translate([0,sy*hs/2,rise-hs/2*tan(a)+0.05]) rotate([-sy*a,0,0]) sv_box([L+2*ov,sl,0.05]);
    sv_m_mabati(c*0.9,rust) translate([0,0,rise+0.08]) rotate([45,0,0]) sv_box([L+2*ov,0.28,0.28]);
    for(sx=[-1,1]) sv_mat(gm,gc) translate([sx*(L/2-t/2),0,0]) rotate([90,0,90])
        linear_extrude(height=t,center=true) polygon([[-D/2,0],[D/2,0],[0,rise]]);
    // A lifted sheet and its missing neighbour.
    if(sv_rnd(seed,3)==0) sv_m_mabati(c*0.8,0.7) translate([sv_rf(seed,-L/3,L/3),-hs*0.6,rise-hs*0.55*tan(a)+0.25]) rotate([a+18,0,8]) sv_box([0.9,1.8,0.03]);
}

// Mono-pitch roof: high at the front (y=-D/2, z=dh), low at the back (z=0).
module sv_part_roof_mono(seed=0,L=8,D=6,dh=0.8,ov=0.4,c=[0.4,0.4,0.4],gc=[0.4,0.4,0.4],gm="block",t=0.3,rust=0.3)
{
    a=atan(dh/D); sl=(D+2*ov)/cos(a);
    sv_m_mabati(c,rust) translate([0,0,dh/2+0.06]) rotate([-a,0,0]) sv_box([L+2*ov,sl,0.05]);
    for(sx=[-1,1]) sv_mat(gm,gc) translate([sx*(L/2-t/2),0,0]) rotate([90,0,90])
        linear_extrude(height=t,center=true) polygon([[-D/2,0],[D/2,0],[-D/2,dh]]);
    sv_m_wood(sv_pole(seed)) for(y=[-D/2+0.4:1.6:D/2-0.2]) translate([0,y,(D/2-y)/D*dh-0.02]) sv_box([L-0.1,0.07,0.1]);
}

// Hipped roof as a solid cap (flat ceiling): corrugated or thatch.
module sv_part_roof_hip(L=8,D=6,rise=1.6,ov=0.5,ridge=-1)
    hull()
    {
        translate([0,0,0.03]) sv_box([L+2*ov,D+2*ov,0.06]);
        translate([0,0,rise]) sv_box([ridge>=0 ? ridge : max(0.2,L-D*0.9),0.2,0.05]);
    }

// Thatch cone for a round hut: stepped courses, a bound top knot.
module sv_part_cone(seed=0,r=3,rise=2.4,c=[0.34,0.28,0.17])
{
    sv_m_thatch(c) cylinder(h=rise,r1=r,r2=0.18,$fn=14);
    sv_m_thatch(c*0.86) translate([0,0,rise*0.02]) cylinder(h=rise*0.34,r1=r+0.12,r2=r*0.7,$fn=14);
    sv_m_thatch(c*0.75) translate([0,0,rise-0.05]) cylinder(h=0.45,r1=0.24,r2=0.1,$fn=7);
}

// Veranda: mabati lean-to on poles along the front of a wall of length L
// (wall face at y=0, veranda towards -y).
module sv_part_veranda(seed=0,L=8,depth=2.0,h=2.5,c=[0.4,0.4,0.4],floor=true)
{
    n=2*max(1,round(L/6));   // even: never a pole on the door axis
    if(floor) sv_m_concrete([0.33,0.32,0.30]) translate([0,-depth/2,0]) sv_slab(L,depth,0.1);
    sv_m_pole(sv_pole(seed)) for(i=[0:n-1]) translate([-L/2+0.2+i*(L-0.4)/(n-1),-depth+0.15,0]) cylinder(h=h-0.3,r=0.07,$fn=6);
    sv_m_pole(sv_pole(seed+1)) translate([0,-depth+0.15,h-0.32]) sv_box([L,0.09,0.11]);
    sv_m_mabati(c,0.4) translate([0,-depth/2-0.1,h-0.1]) rotate([10,0,0]) sv_box([L+0.3,depth+0.5,0.04]);
}

// Hand-painted signboard above a door (letters are slightly raised paint).
module sv_part_sign(msg="DUKA",w=3,c=[0.1,0.25,0.4])
{
    sv_m_enamel(c) sv_box([w,0.04,0.55]);
    sv_m_enamel([0.47,0.45,0.38]) translate([0,-0.024,0]) rotate([90,0,0])
        linear_extrude(height=0.008,center=true) text(msg,size=0.3,halign="center",valign="center");
}

// Rubbish, fallen sheets and creeping grass around an abandoned building.
module sv_part_decay(seed=0,L=8,D=6)
{
    sv_m_mabati(sv_zinc(seed),0.7) translate([sv_rf(seed,-L/2,L/2),D/2+0.9,0.04]) rotate([0,3,sv_rnd(seed,90)]) sv_box([0.9,2.2,0.03]);
    color([0.33,0.30,0.16]) for(i=[0:3]) translate([-L/2+0.3+sv_rf(seed+i,0,L-0.6),(i%2==0 ? -1 : 1)*(D/2+0.25),0])
        for(k=[0:3]) rotate([sv_rf(seed+i+k,-18,18),sv_rf(seed+i*3+k,-18,18),0]) cylinder(h=sv_rf(seed+k,0.5,0.9),r1=0.03,r2=0.005,$fn=3);
}

// ================= Buildings =================
// Duka: the upcountry shop. Rendered blockwork, enamel dado and shopfront, a
// wide steel double door on the axis, small barred windows, a mono-pitch
// mabati roof behind a low parapet and a veranda on poles. sign="" leaves the
// board off.
module sv_bldg_duka(seed=0,L=7,D=8,sign="DUKA")
{
    H=2.7; dh=0.7; t=0.3; c=sv_wash(seed); ec=sv_enamel(seed+1); zc=sv_zinc(seed+2);
    dw=min(2.2,L*0.34); wx=L/2-(L/2-dw/2)/2-0.1; win=L>6.2;
    fops=concat(win ? [[-wx,1.0,0.9,1.0]] : [],[[0,0,dw,2.2]],win ? [[wx,1.0,0.9,1.0]] : []);
    sv_part_floor(seed,L,D);
    sv_part_shell(L,D,H,t,c,"block",fops,[[L*0.28,1.2,0.7,0.8]],[[0,1.2,0.8,0.8]],H+dh+0.35);
    translate([0,-D/2+t/2,0]) sv_part_dado(L,1.05,fops,ec,t);
    translate([0,0,H]) sv_part_roof_mono(seed,L,D,dh,0.25,zc,c,"block",t,0.25+0.1*sv_rnd(seed,3));
    // Shopfront: door leaves folded back, windows, signboard, counter grille.
    for(sx=[-1,1]) translate([sx*dw/2,-D/2+t,0]) sv_flipx(sx) translate([-dw/4,0,0]) rotate([0,0,180]) sv_part_leaf(seed+sx,dw/2,2.2,-80-sv_rnd(seed+sx,20));
    if(win) for(sx=[-1,1]) translate([sx*wx,-D/2,1.5]) sv_part_window(seed+sx+7,0.9,1.0);
    translate([L*0.28,D/2,1.6]) rotate([0,0,180]) sv_part_window(seed+9,0.7,0.8);
    if(sign!="") translate([0,-D/2-0.03,H+dh*0.55+0.1]) sv_part_sign(sign,min(L-0.8,3.4),ec);
    translate([0,-D/2,0]) sv_part_veranda(seed+3,L,2.0,2.55,zc);
    // Inner skin: limewash above an enamel dado.
    translate([0,-D/2+t+0.006,0]) rotate([0,0,180]) sv_part_dado(L-2*t,1.2,fops,ec*0.8,0);
    sv_part_decay(seed,L,D);
}

// Mabati house: the ordinary dwelling of a trading centre or a shamba. Walls
// by seed: rendered block, mud plaster or iron sheets nailed to a pole frame.
// Gable roof, door on the axis, shuttered windows, a water barrel at the eaves.
module sv_bldg_house(seed=0,L=8,D=6,wall="")
{
    m=wall!="" ? wall : ["block","mud","iron","mud"][sv_rnd(seed,4)];
    H=2.45; t=m=="iron" ? 0.12 : 0.3; rise=1.25;
    c=m=="block" ? sv_wash(seed) : m=="mud" ? sv_mud(seed) : sv_zinc(seed+5); zc=sv_zinc(seed+2);
    wx=L*0.3;
    fops=[[-wx,0.95,0.8,0.95],[0,0,0.95,2.05],[wx,0.95,0.8,0.95]];
    sv_part_floor(seed,L,D,m=="mud");
    sv_part_shell(L,D,H,t,c,m,fops,[[-wx,1.0,0.8,0.9]],[[0,1.0,0.8,0.9]]);
    if(m=="block") translate([0,-D/2+t/2,0]) sv_part_dado(L,0.9,fops,sv_enamel(seed+1),t);
    translate([0,0,H]) sv_part_roof_gable(seed,L,D,rise,0.5,zc,c,m,t,0.3+0.1*sv_rnd(seed+1,3));
    translate([0,-D/2+t,0]) rotate([0,0,180]) sv_part_leaf(seed,0.95,2.05,-75);
    for(sx=[-1,1]) translate([sx*wx,-D/2,1.42]) sv_part_window(seed+sx+3,0.8,0.95);
    translate([wx,D/2,1.45]) rotate([0,0,180]) sv_part_window(seed+11,0.8,0.9);
    // Pole frame showing at the corners of iron and mud houses.
    if(m!="block") sv_m_pole(sv_pole(seed)) for(sx=[-1,1],sy=[-1,1]) translate([sx*(L/2-0.05),sy*(D/2-0.05),0]) cylinder(h=H,r=0.07,$fn=5);
    // Gutter into a drum: every roof harvests rain.
    sv_m_iron([0.22,0.24,0.26]) translate([L/2-0.5,-D/2-0.75,0]) cylinder(h=0.88,r=0.29,$fn=10);
    sv_part_decay(seed,L,D);
}

// Round mud hut (rondavel) under a thatch cone. L=D=outer diameter.
module sv_bldg_rondavel(seed=0,L=6,D=6)
{
    r=min(L,D)/2; H=2.15; t=0.3; n=12; c=sv_mud(seed); seg=2*(r-t/2)*tan(180/n)+0.02;
    sv_m_mud(c*0.8) cylinder(h=0.1,r=r,$fn=n);
    // Twelve wall panels; the one facing -y is the doorway.
    for(i=[0:n-1]) let(a=i*360/n-90) if(i!=0)
        sv_m_mud(c) rotate([0,0,a]) translate([r-t/2,0,H/2]) sv_box([t,seg,H]);
    sv_m_mud(c) rotate([0,0,-90]) translate([r-t/2,0,(H+2.0)/2]) sv_box([t,seg,H-2.0]);
    sv_m_pole(sv_pole(seed)) for(sx=[-1,1]) translate([sx*seg/2,-r+t/2,0]) cylinder(h=2.0,r=0.06,$fn=5);
    // Limewashed band and a painted plinth, as on a well-kept homestead.
    if(sv_rnd(seed,2)==0) for(i=[1:n-1]) let(a=i*360/n-90) sv_m_enamel(sv_enamel(seed)*0.7)
        rotate([0,0,a]) translate([r+0.004,0,0.3]) sv_box([0.01,seg,0.6]);
    translate([0,0,H-0.05]) sv_part_cone(seed,r+0.65,r*0.95,sv_thatch(seed));
    sv_m_pole(sv_pole(seed+2)) for(i=[0:5]) rotate([0,0,i*60+20]) translate([r+0.5,0,0]) cylinder(h=H-0.25,r=0.05,$fn=5);
}

// Maasai house (enkaji): a low loaf of wattle plastered with dung and earth,
// rounded corners, a flat-domed roof, the doorway hooded by a short curved
// porch on the long front side. Raised to walk-in height for play.
module sv_bldg_manyatta(seed=0,L=6.5,D=5)
{
    H=2.05; t=0.28; c=sv_dung(seed); k=0.9;   // k: corner chamfer
    sv_m_mud(c*0.75) linear_extrude(height=0.1) polygon([[-L/2+k,-D/2],[L/2-k,-D/2],[L/2,-D/2+k],[L/2,D/2-k],[L/2-k,D/2],[-L/2+k,D/2],[-L/2,D/2-k],[-L/2,-D/2+k]]);
    dx=0;
    translate([0,-D/2+t/2,0]) sv_part_wall(L-2*k,H,[[dx,0,0.9,1.9]],t,c,"mud");
    translate([0,D/2-t/2,0]) sv_part_wall(L-2*k,H,[],t,c,"mud");
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,90]) sv_part_wall(D-2*k,H,[[0,1.3,0.3,0.3]],t,c,"mud");
    for(sx=[-1,1],sy=[-1,1]) sv_m_mud(c) translate([sx*(L/2-k/2),sy*(D/2-k/2),H/2]) rotate([0,0,sx*sy*45]) sv_box([t,k*1.414+0.2,H]);
    // Roof: a low dung-plastered dome, cracked, grass taking root.
    sv_m_mud(c*0.9) translate([0,0,H]) scale([(L+0.3)/2,(D+0.3)/2,1]) cylinder(h=0.42,r1=1,r2=0.62,$fn=12);
    // Hooded entrance.
    sv_m_mud(c) { translate([dx-0.62,-D/2-0.55,1.0]) sv_box([t*0.8,1.1,2.0]); translate([dx+0.2,-D/2-1.0,2.0]) sv_box([1.9,0.5,0.22]); translate([dx,-D/2-0.5,2.0]) sv_box([1.5,1.1,0.2]); }
    sv_m_pole(sv_pole(seed)) for(i=[0:4]) translate([-L/2+0.6+i*(L-1.2)/4,D/2+0.12,0]) rotate([8,0,0]) cylinder(h=H+0.2,r=0.04,$fn=4);
    color([0.33,0.30,0.16]) for(i=[0:4]) translate([sv_rf(seed+i,-L/3,L/3),sv_rf(seed+i*7,-D/3,D/3),H+0.4]) cylinder(h=0.35,r1=0.03,r2=0.004,$fn=3);
}

// Mission church: a plain block hall with the door in the front gable, tall
// narrow windows, a porch on poles, a cross on the ridge and an open bell
// frame. The nave runs along y (front gable faces -y), so D > L.
module sv_bldg_church(seed=0,L=9,D=16)
{
    H=3.6; t=0.32; rise=L*0.38; c=sv_wash(seed); zc=sv_zinc(seed+2);
    nb=max(2,floor(D/3.2));
    sops=[for(i=[0:nb-1]) [-(D-2*t)/2+(i+0.5)*(D-2*t)/nb,1.1,0.7,1.9]];
    sv_part_floor(seed,L,D);
    translate([0,-D/2+t/2,0]) sv_part_wall(L,H,[[0,0,1.7,2.5]],t,c,"block");
    translate([0,D/2-t/2,0]) sv_part_wall(L,H,[],t,c,"block");
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90]) sv_part_wall(D-2*t,H,sops,t,c,"block");
    translate([0,-D/2+t/2,0]) sv_part_dado(L,1.0,[[0,0,1.7,2.5]],sv_enamel(seed+1),t);
    // Roof ridge along y: build the gable roof rotated a quarter turn.
    translate([0,0,H]) rotate([0,0,90]) sv_part_roof_gable(seed,D,L,rise,0.5,zc,c,"block",t,0.2);
    // Round gable window, cross, porch.
    sv_m_enamel(sv_dark()) translate([0,-D/2-0.01,H+rise*0.35]) rotate([90,0,0]) cylinder(h=0.02,r=0.45,$fn=10);
    sv_m_enamel([0.44,0.43,0.40]) translate([0,-D/2,H+rise]) { translate([0,0,0.7]) sv_box([0.12,0.12,1.4]); translate([0,0,0.95]) sv_box([0.7,0.12,0.12]); }
    translate([0,-D/2,0]) sv_part_veranda(seed+3,3.4,1.8,2.7,zc);
    for(sx=[-1,1]) translate([sx*0.85,-D/2+t,0]) sv_flipx(sx) translate([-0.42,0,0]) rotate([0,0,180]) sv_part_leaf(seed+sx,0.84,2.4,-70);
    // Bell frame beside the porch.
    translate([L/2+1.6,-D/2+1.0,0])
    {
        sv_m_pole(sv_pole(seed)) { for(sx=[-1,1]) translate([sx*0.6,0,0]) cylinder(h=3.4,r=0.08,$fn=6); translate([0,0,3.3]) sv_box([1.6,0.12,0.12]); }
        sv_m_iron([0.30,0.24,0.12]) translate([0,0,2.7]) cylinder(h=0.5,r1=0.28,r2=0.1,$fn=8);
    }
    sv_part_decay(seed,L,D);
}

// Long institutional block: school classrooms, a dispensary, staff quarters.
// One room deep, a door on the axis and one per further bay, steel windows,
// gable mabati roof oversailing a veranda. kind: "school" | "clinic" | "staff".
module sv_bldg_block(seed=0,L=20,D=7,kind="school")
{
    H=2.9; t=0.3; rise=1.5; c=sv_wash(seed); zc=sv_zinc(seed+2);
    ec=kind=="clinic" ? [0.12,0.30,0.20] : kind=="school" ? [0.10,0.22,0.38] : sv_enamel(seed+1);
    nb=max(1,2*floor((L/5.5-1)/2)+1); bw=L/nb;   // odd bay count: a door on the axis
    fops=[for(i=[0:nb-1]) let(x=-L/2+(i+0.5)*bw) each [[x-bw*0.28,1.0,1.2,1.1],[x,0,1.0,2.1],[x+bw*0.28,1.0,1.2,1.1]]];
    bops=[for(i=[0:nb-1]) let(x=-L/2+(i+0.5)*bw) each [[x-bw*0.22,1.2,1.2,0.9],[x+bw*0.22,1.2,1.2,0.9]]];
    sv_part_floor(seed,L,D);
    sv_part_shell(L,D,H,t,c,"block",fops,bops,[]);
    translate([0,-D/2+t/2,0]) sv_part_dado(L,1.0,fops,ec,t);
    translate([0,-1.0,H]) sv_part_roof_gable(seed,L,D+2.0,rise,0.4,zc,c,"block",t,0.2);
    // Partition walls between rooms (door-height opening at the back of each).
    if(nb>1) for(i=[1:nb-1]) translate([-L/2+i*bw,0,0]) rotate([0,0,90]) sv_part_wall(D-2*t,H,[[(D-2*t)/2-0.8,0,1.0,2.1]],0.15,c*0.95,"block");
    for(i=[0:nb-1]) let(x=-L/2+(i+0.5)*bw)
    {
        translate([x,-D/2+t,0]) rotate([0,0,180]) sv_part_leaf(seed+i,1.0,2.1,-60-sv_rnd(seed+i,40));
        for(sx=[-1,1]) translate([x+sx*bw*0.28,-D/2,1.55]) sv_part_window(seed+i*3+sx,1.2,1.1);
        // Blackboard / notice board on the partition side.
        if(kind=="school") sv_m_enamel([0.05,0.07,0.06]) translate([x+bw/2-0.34,0.3,1.5]) sv_box([0.03,2.4,1.1]);
    }
    // Veranda posts under the oversailing roof.
    sv_m_concrete([0.33,0.32,0.30]) translate([0,-D/2-1.0,0]) sv_slab(L,2.0,0.1);
    sv_m_pole(sv_pole(seed)) for(i=[0:nb]) translate([-L/2+0.15+i*(L-0.3)/nb,-D/2-1.85,0]) cylinder(h=H+0.1,r=0.07,$fn=6);
    if(kind=="clinic") translate([0,-D/2-0.03,H-0.35]) sv_part_sign("DISPENSARY",3.6,ec);
    if(kind=="school") translate([0,-D/2-0.03,H-0.35]) sv_part_sign("STD 4",1.6,ec);
    sv_part_decay(seed,L,D);
}

// Ranger / police office: olive-painted block, hipped mabati roof, a veranda
// across the front, barred windows, whip aerial.
module sv_bldg_ranger(seed=0,L=11,D=8)
{
    H=2.8; t=0.32; c=[0.36,0.37,0.30]; zc=[0.20,0.26,0.18];
    wx=L*0.3;
    fops=[[-wx,1.0,1.0,1.0],[0,0,1.1,2.1],[wx,1.0,1.0,1.0]];
    sv_part_floor(seed,L,D);
    sv_part_shell(L,D,H,t,c,"block",fops,[[-wx,1.2,0.8,0.8],[wx,1.2,0.8,0.8]],[[0,1.2,0.8,0.8]]);
    translate([0,-D/2+t/2,0]) sv_part_dado(L,1.0,fops,sv_olive(),t);
    sv_m_mabati(zc,0.15) translate([0,0,H]) sv_part_roof_hip(L,D,1.7,0.6);
    translate([0,-D/2,0]) sv_part_veranda(seed,L,2.0,2.6,zc);
    translate([0,-D/2+t,0]) rotate([0,0,180]) sv_part_leaf(seed,1.1,2.1,-70);
    for(sx=[-1,1]) translate([sx*wx,-D/2,1.5]) sv_part_window(1,1.0,1.0);
    translate([0,-D/2-0.03,H-0.3]) sv_part_sign("WARDEN",2.4,sv_olive());
    sv_m_iron() translate([L/2-0.6,D/2-0.6,H+1.2]) cylinder(h=4.5,r1=0.03,r2=0.008,$fn=4);
    sv_part_decay(seed,L,D);
}

// Uniport: the round prefabricated metal hut of every police and ranger line.
module sv_bldg_uniport(seed=0,L=5.4,D=5.4)
{
    r=min(L,D)/2; H=2.2; n=12; t=0.08; c=sv_rnd(seed,2)==0 ? [0.36,0.37,0.36] : [0.24,0.29,0.22]; seg=2*(r-t/2)*tan(180/n)+0.02;
    sv_m_concrete([0.33,0.32,0.30]) cylinder(h=0.1,r=r+0.1,$fn=n);
    for(i=[1:n-1]) let(a=i*360/n-90) sv_m_mabati(c,0.2) rotate([0,0,a]) translate([r-t/2,0,H/2]) sv_box([t,seg,H]);
    sv_m_mabati(c,0.2) rotate([0,0,-90]) translate([r-t/2,0,(H+2.0)/2]) sv_box([t,seg,H-2.0]);
    sv_m_mabati(c*0.9,0.3) translate([0,0,H]) cylinder(h=1.25,r1=r+0.25,r2=0.25,$fn=n);
    sv_m_iron() translate([0,0,H+1.25]) cylinder(h=0.25,r=0.2,$fn=8);
    translate([0,-r+t,0]) rotate([0,0,180]) sv_part_leaf(seed,seg-0.1,2.0,-95);
}

// Banda: the open mess / lounge of a safari camp. Dry-stone half walls,
// timber posts, a steep thatched hip roof on a pole frame, a bar at the back.
module sv_bldg_banda(seed=0,L=12,D=9)
{
    H=2.7; t=0.4; c=sv_stone(seed); hw=1.0;
    sv_m_stone(c*0.9) sv_slab(L,D,0.1);
    gk_material([0.26,0.17,0.10],roughness=0.3) translate([0,0,0.1]) sv_box([L-1.2,D-1.2,0.006]);
    // Half walls with a wide opening on the front axis and a gap at the back.
    translate([0,-D/2+t/2,0]) sv_part_wall(L,hw,[[0,0,3.0,hw]],t,c,"stone");
    translate([0,D/2-t/2,0]) sv_part_wall(L,H,[[-L*0.3,1.0,1.4,1.2],[L*0.3,1.0,1.4,1.2]],t,c,"stone");
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,90]) sv_part_wall(D-2*t,hw,[],t,c,"stone");
    sv_m_pole(sv_pole(seed)) for(sx=[-1,1],x=[1.5,L/2-0.2]) for(sy=[-1,1]) translate([sx*x,sy*(D/2-0.2),0]) cylinder(h=H,r=0.11,$fn=6);
    sv_m_pole(sv_pole(seed+1)) for(sy=[-1,1]) translate([0,sy*(D/2-0.2),H-0.08]) sv_box([L,0.14,0.16]);
    sv_m_thatch(sv_thatch(seed)) translate([0,0,H]) sv_part_roof_hip(L,D,D*0.48,1.0);
    sv_m_thatch(sv_thatch(seed)*0.8) translate([0,0,H+D*0.48]) sv_box([max(0.6,L-D*0.9)+0.5,0.5,0.3]);
    // Bar counter along the back wall.
    sv_m_wood([0.26,0.17,0.10]) translate([0,D/2-1.5,0.1]) { sv_slab(L*0.4,0.6,1.0); translate([0,0,1.0]) sv_slab(L*0.4+0.3,0.8,0.06); }
}

// Safari tent: canvas on a pole frame over a low timber deck, an outer fly,
// the front flaps tied back, a shaded veranda with a pole rail.
module sv_bldg_tent(seed=0,L=5,D=7)
{
    H=1.9; rise=1.1; c=sv_canvas(seed); t=0.05;
    sv_m_wood([0.27,0.19,0.12]) translate([0,-0.9,0]) sv_slab(L+0.4,D+2.2,0.1);
    sv_part_shell(L,D,H,t,c,"canvas",[[0,0,1.5,H-0.05]],[[0,0.9,1.2,0.7]],[[0,0.9,1.4,0.7]]);
    // Inner tent roof (ridge along y) and the fly above it.
    a=atan(rise/(L/2)); sl=(L/2)/cos(a);
    for(sx=[-1,1]) sv_m_canvas(c) translate([sx*L/4,0,H+rise/2]) rotate([0,sx*a,0]) sv_box([sl,D,t]);
    for(sy=[-1,1]) sv_m_canvas(c) translate([0,sy*(D/2-t/2),H]) rotate([90,0,0]) linear_extrude(height=t,center=true) polygon([[-L/2,0],[L/2,0],[0,rise]]);
    f=sv_canvas(seed+1)*0.9; fl=(L/2+0.7)/cos(a);
    for(sx=[-1,1]) sv_m_canvas(f) translate([sx*(L/4+0.35),-0.9,H+rise/2+0.12-0.35*tan(a)]) rotate([0,sx*a,0]) sv_box([fl,D+2.0,t]);
    // Ridge pole on a rear upright and a front A-frame that leaves the door axis clear.
    sv_m_pole(sv_pole(seed))
    {
        translate([0,D/2+0.05,0]) cylinder(h=H+rise+0.2,r=0.05,$fn=5);
        for(sx=[-1,1]) sv_rod([sx*(L/2+0.1),-D/2-1.8,0],[0,-D/2-1.8,H+rise+0.2],0.05,5);
        translate([0,-0.9,H+rise+0.16]) sv_box([0.07,D+2.0,0.07]);
    }
    // Tied-back door flaps, veranda rail, guy ropes.
    for(sx=[-1,1]) sv_m_canvas(c*0.9) translate([sx*0.95,-D/2-0.06,H/2]) rotate([0,0,sx*20]) sv_box([0.5,0.06,H-0.1]);
    sv_m_pole(sv_pole(seed+2)) { for(sx=[-1,1]) translate([sx*(L/2+0.1),-D/2-1.8,0]) cylinder(h=1.0,r=0.05,$fn=5); for(sx=[-1,1]) translate([sx*(L/4+0.45),-D/2-1.8,0.95]) sv_box([L/2-0.7,0.06,0.06]); }
    color([0.30,0.27,0.20]) for(sx=[-1,1],y=[-D/2,0,D/2]) sv_rod([sx*(L/2+0.6),y,H-0.1],[sx*(L/2+1.5),y,0],0.012,3);
}

// Godown / posho mill / workshop: a tall block shed with a wide door and a
// mabati gable roof on steel trusses, the wall vented by a row of holes.
module sv_bldg_store(seed=0,L=12,D=8)
{
    H=3.6; t=0.32; rise=1.5; c=sv_wash(seed+3); zc=sv_zinc(seed+2);
    dw=min(3.2,L*0.32);
    vents=[for(x=[-L/2+1.3:2.2:-dw/2-0.8]) [x,2.6,0.8,0.5]];
    nv=len(vents);
    fops=concat(vents,[[0,0,dw,3.0]],nv>0 ? [for(i=[1:nv]) [-vents[nv-i][0],2.6,0.8,0.5]] : []);
    sv_part_floor(seed,L,D);
    sv_part_shell(L,D,H,t,c,"block",fops,[for(x=[-L/2+1.5:2.6:L/2-1.0]) [x,2.4,1.0,0.6]],[[0,2.4,1.0,0.6]]);
    translate([0,0,H]) sv_part_roof_gable(seed,L,D,rise,0.45,zc,c,"iron",t,0.4);
    // Sliding door half open on its rail.
    sv_m_mabati(sv_paintiron(seed),0.35) translate([dw*0.75,-D/2-0.1,1.5]) sv_box([dw,0.05,3.0]);
    sv_m_iron() { translate([dw*0.3,-D/2-0.1,3.05]) sv_box([dw*2.2,0.07,0.08]); for(x=[-L/2+2:L/3:L/2-1]) translate([x,0,H-0.1]) sv_box([0.08,D-2*t,0.12]); }
    translate([0,-D/2+t/2,0]) sv_part_dado(L,1.2,fops,sv_enamel(seed+1),t);
    sv_part_decay(seed,L,D);
}

// ================= Props =================
// Thorn fence (olale): cut acacia branches piled into a barrier, L along x.
module sv_prop_thorn(seed=0,L=6,h=1.4)
{
    // Kept to a few dozen triangles a metre: a boma ring is 120 m of this.
    n=max(2,round(L/1.5));
    for(i=[0:n-1]) let(x=-L/2+(i+0.5)*L/n,s=seed+i*7)
    {
        // Spiky heaps, not boulders: tilted cones of cut branches.
        color(i%2==0 ? [0.20,0.16,0.11] : [0.24,0.19,0.13]) for(k=[0:2]) translate([x+(k-1)*0.45,sv_rf(s+k,-0.3,0.3),0])
            rotate([sv_rf(s+k*3,-38,38),sv_rf(s+k*5,-38,38),sv_rnd(s+k,90)]) cylinder(h=h*sv_rf(s+k*7,0.8,1.15),r1=0.55,r2=0.03,$fn=5);
        color([0.26,0.23,0.18]) for(k=[0:2]) translate([x+sv_rf(s+k*5,-0.6,0.6),sv_rf(s+k*7,-0.3,0.3),0.1]) rotate([sv_rf(s+k,-65,65),sv_rf(s+k*3,-65,65),0]) cylinder(h=h*sv_rf(s+k*11,0.9,1.4),r1=0.04,r2=0.006,$fn=3);
    }
}

// Ring of thorn fence with one gap (a boma / cattle kraal). Gap faces -y.
module sv_prop_kraal(seed=0,r=8,gap=3.2,h=1.4)
{
    n=max(6,round(2*3.1416*r/4)); ga=gap/r*57.3;
    for(i=[0:n-1]) let(a=-90+ga/2+(i+0.5)*(360-ga)/n)
        rotate([0,0,a]) translate([r,0,0]) rotate([0,0,90]) sv_prop_thorn(seed+i*11,2*r*sin((360-ga)/n/2)+0.5,h);
    // Trampled dung floor.
    color([0.21,0.17,0.12]) cylinder(h=0.03,r=r-0.6,$fn=14);
}

// Granary: a woven basket on a forked-pole platform under a thatch cap.
module sv_prop_granary(seed=0)
{
    sv_m_pole(sv_pole(seed)) { for(a=[45:90:315]) rotate([0,0,a]) translate([0.8,0,0]) cylinder(h=0.9,r=0.07,$fn=5); translate([0,0,0.85]) cylinder(h=0.1,r=1.2,$fn=8); }
    sv_m_thatch([0.34,0.27,0.16]) translate([0,0,0.95]) cylinder(h=1.5,r1=0.85,r2=1.1,$fn=10);
    translate([0,0,2.4]) sv_part_cone(seed,1.5,1.1,sv_thatch(seed));
}

// 5000 l plastic water tank on a block plinth, fed by a downpipe.
module sv_prop_tank(seed=0)
{
    c=sv_rnd(seed,2)==0 ? [0.05,0.06,0.06] : [0.10,0.25,0.15];
    sv_m_concrete([0.36,0.35,0.33]) cylinder(h=0.7,r=1.25,$fn=8);
    sv_m_plastic(c) translate([0,0,0.7]) { cylinder(h=1.9,r=1.05,$fn=14); translate([0,0,1.9]) cylinder(h=0.3,r1=1.05,r2=0.4,$fn=14); for(z=[0.4,0.95,1.5]) translate([0,0,z]) cylinder(h=0.08,r=1.08,$fn=14); }
    sv_m_iron() translate([1.1,0,0.85]) rotate([0,90,0]) cylinder(h=0.25,r=0.03,$fn=5);
}

// Elevated steel tank on a braced angle-iron tower (borehole supply).
module sv_prop_tank_tower(seed=0,h=7)
{
    sv_m_iron([0.22,0.23,0.24])
    {
        for(sx=[-1,1],sy=[-1,1]) translate([sx*1.1,sy*1.1,0]) cylinder(h=h,r=0.06,$fn=4);
        for(z=[h*0.33,h*0.66,h-0.05]) for(a=[0,90]) rotate([0,0,a]) for(s=[-1,1]) translate([0,s*1.1,z]) sv_box([2.3,0.06,0.06]);
        for(a=[0:90:270]) rotate([0,0,a]) sv_rod([-1.1,1.1,0.2],[1.1,1.1,h*0.33],0.025,3);
    }
    sv_m_mabati([0.38,0.39,0.40],0.25) translate([0,0,h]) cylinder(h=2.0,r=1.45,$fn=12);
    sv_m_iron() translate([0,0,h+2.0]) cylinder(h=0.3,r1=1.5,r2=0.3,$fn=12);
}

// Concrete cattle trough with a float box.
module sv_prop_trough(seed=0)
{
    sv_m_concrete([0.36,0.35,0.32]) { for(sy=[-1,1]) translate([0,sy*0.4,0.25]) sv_box([3.6,0.12,0.5]); for(sx=[-1,1]) translate([sx*1.74,0,0.25]) sv_box([0.12,0.8,0.5]); sv_slab(3.6,0.9,0.12); }
    gk_material([0.05,0.06,0.04],roughness=0.03) translate([0,0,0.36]) sv_box([3.4,0.7,0.01]);
}

// Market stall: pole frame, sacking and iron roof, a plank table.
module sv_prop_stall(seed=0)
{
    sv_m_pole(sv_pole(seed)) { for(sx=[-1,1],sy=[-1,1]) translate([sx*1.2,sy*0.9,0]) cylinder(h=sy<0 ? 2.1 : 1.8,r=0.05,$fn=5); translate([0,0,0.78]) sv_box([2.2,1.0,0.06]); for(sx=[-1,1]) translate([sx*0.95,0,0.39]) sv_box([0.08,0.9,0.78]); }
    if(sv_rnd(seed,2)==0) sv_m_mabati(sv_zinc(seed),0.6) translate([0,0,1.98]) rotate([9,0,0]) sv_box([2.8,2.2,0.03]);
    else sv_m_canvas([0.36,0.32,0.22]) translate([0,0,1.98]) rotate([9,0,0]) sv_box([2.8,2.2,0.03]);
}

// Reserve gate: two stone piers, a pole lintel with the name board, a boom.
module sv_prop_gate_arch(seed=0,w=7,msg="TALEK GATE")
{
    for(sx=[-1,1]) sv_m_stone(sv_stone(seed)) translate([sx*(w/2+0.6),0,0]) { sv_slab(1.2,1.2,3.4); translate([0,0,3.4]) sv_slab(1.5,1.5,0.2); }
    sv_m_pole(sv_pole(seed)) for(z=[3.9,4.6]) translate([0,0,z]) rotate([0,90,0]) cylinder(h=w+3.2,r=0.11,center=true,$fn=6);
    translate([0,-0.14,4.25]) sv_part_sign(msg,min(w,4.4),sv_olive());
    sv_m_thatch(sv_thatch(seed)) translate([0,0,4.75]) rotate([0,0,0]) sv_part_roof_hip(w+3.2,1.6,0.9,0.2);
    // Boom barrier, raised.
    sv_m_enamel([0.45,0.44,0.40]) translate([-w/2+0.2,-1.0,1.0]) rotate([0,-55,0]) translate([w*0.45,0,0]) sv_box([w*0.9,0.09,0.09]);
    sv_m_enamel([0.42,0.10,0.07]) translate([-w/2+0.2,-1.0,1.0]) rotate([0,-55,0]) for(i=[1:2:5]) translate([i*w*0.16,0,0]) sv_box([w*0.08,0.1,0.1]);
    sv_m_iron() translate([-w/2+0.2,-1.0,0]) cylinder(h=1.05,r=0.09,$fn=6);
}

// Guyed lattice radio mast.
module sv_prop_mast(seed=0,h=22)
{
    sv_m_iron([0.34,0.12,0.09]) { for(a=[0,120,240]) rotate([0,0,a]) translate([0.22,0,0]) cylinder(h=h,r=0.03,$fn=4); for(z=[1:1.5:h-0.5]) translate([0,0,z]) cylinder(h=0.04,r=0.26,$fn=3); }
    color([0.25,0.25,0.25]) for(a=[60,180,300],z=[h*0.5,h*0.9]) sv_rod([0,0,z],[h*0.35*cos(a),h*0.35*sin(a),0],0.012,3);
    sv_m_iron() translate([0,0,h]) cylinder(h=2.4,r1=0.03,r2=0.008,$fn=4);
}

module sv_prop_flagpole(seed=0)
{
    sv_m_enamel([0.44,0.44,0.42]) cylinder(h=7,r1=0.05,r2=0.03,$fn=6);
    sv_m_concrete([0.36,0.35,0.33]) cylinder(h=0.3,r=0.5,$fn=8);
    sv_m_canvas([0.30,0.09,0.07]) translate([0.55,0,6.3]) rotate([0,8,0]) sv_box([1.0,0.02,0.6]);
}

// Wooden power / telephone pole with a cross arm and a fallen wire.
module sv_prop_pole(seed=0,h=7)
{
    sv_m_pole([0.22,0.18,0.14]) rotate([sv_rf(seed,-3,3),sv_rf(seed+1,-3,3),0]) { cylinder(h=h,r1=0.11,r2=0.07,$fn=6); translate([0,0,h-0.5]) sv_box([1.4,0.08,0.1]); }
}

// Yellow jerrycans and a cut drum: the water economy of a dry town.
module sv_prop_jerrycans(seed=0)
{
    for(i=[0:2+sv_rnd(seed,3)]) sv_m_plastic(i%3==2 ? [0.10,0.18,0.30] : [0.46,0.36,0.05])
        translate([i*0.34+sv_rf(seed+i,-0.04,0.04),sv_rf(seed+i*3,-0.25,0.25),0]) rotate([sv_rnd(seed+i,5)==0 ? 90 : 0,0,sv_rnd(seed+i,40)-20]) translate([0,0,0.21]) { sv_box([0.27,0.16,0.42]); translate([0.07,0,0.24]) cylinder(h=0.06,r=0.03,$fn=5); }
}

// Charcoal and maize sacks stacked for the lorry that never came.
module sv_prop_sacks(seed=0)
{
    for(i=[0:4]) sv_m_canvas(i%2==0 ? [0.36,0.33,0.26] : [0.30,0.27,0.20])
        translate([(i%3-1)*0.55,floor(i/3)*0.05,0.2+floor(i/3)*0.36]) rotate([0,0,sv_rnd(seed+i,30)-15]) scale([1,0.62,0.42]) sphere(r=0.46,$fn=7);
}

// Camp fire circle: stone ring, ash, log seats and canvas chairs.
module sv_prop_firepit(seed=0)
{
    sv_m_stone(sv_stone(seed)) for(a=[0:36:324]) rotate([0,0,a]) translate([0.9,0,0.1]) scale([1,1.3,0.8]) sphere(r=0.2,$fn=5);
    color([0.09,0.09,0.08]) cylinder(h=0.05,r=0.8,$fn=10);
    sv_m_pole([0.12,0.10,0.09]) for(a=[20,140,250]) rotate([0,0,a]) translate([0.25,0,0.1]) rotate([0,80,0]) cylinder(h=0.8,r=0.07,$fn=5);
    for(a=[30,150,270]) rotate([0,0,a]) translate([2.3,0,0])
    {
        sv_m_pole(sv_pole(seed)) for(sx=[-1,1],sy=[-1,1]) translate([sx*0.22,sy*0.25,0]) rotate([sx*-8,0,0]) cylinder(h=0.85,r=0.02,$fn=4);
        sv_m_canvas(sv_canvas(seed+a)) { translate([0,0,0.42]) sv_box([0.5,0.5,0.03]); translate([0.24,0,0.7]) rotate([0,-12,0]) sv_box([0.03,0.5,0.4]); }
    }
}

// Termite mound: fluted red-earth chimney, sometimes with a tree growing out.
module sv_prop_termite(seed=0)
{
    h=sv_rf(seed,1.6,3.2);
    sv_m_mud([0.36,0.20,0.12]) { cylinder(h=h*0.55,r1=h*0.42,r2=h*0.2,$fn=7); for(i=[0:2]) translate([sv_rf(seed+i,-0.3,0.3),sv_rf(seed+i*3,-0.3,0.3),h*0.3]) rotate([sv_rf(seed+i,-8,8),sv_rf(seed+i*5,-8,8),0]) cylinder(h=h*sv_rf(seed+i*7,0.5,0.7),r1=h*0.16,r2=h*0.05,$fn=6); }
}

// Granite boulders (a kopje fragment): cover in the open plain.
module sv_prop_rocks(seed=0,s=1)
{
    for(i=[0:3+sv_rnd(seed,3)]) sv_m_stone(i%2==0 ? [0.34,0.31,0.28] : [0.29,0.27,0.25])
        translate([sv_rf(seed+i*3,-2.2,2.2)*s,sv_rf(seed+i*5,-1.8,1.8)*s,sv_rf(seed+i,0.2,0.6)*s]) rotate([sv_rnd(seed+i,30),sv_rnd(seed+i*7,30),sv_rnd(seed+i*9,90)])
            scale([1.3,1,0.8]) sphere(r=sv_rf(seed+i*11,0.7,1.7)*s,$fn=6);
}

// Bleached wildebeest skull and ribs.
module sv_prop_bones(seed=0)
{
    color([0.46,0.44,0.38])
    {
        translate([0,0,0.12]) scale([1.6,0.8,0.7]) sphere(r=0.16,$fn=6);
        for(sx=[-1,1]) translate([0.05,sx*0.1,0.2]) rotate([sx*-50,0,0]) rotate([0,0,sx*20]) cylinder(h=0.4,r1=0.035,r2=0.008,$fn=4);
        translate([0.85,0,0.05]) rotate([0,90,0]) cylinder(h=1.0,r=0.03,center=true,$fn=4);
        for(i=[0:5],sy=[-1,1]) translate([0.5+i*0.13,0,0.05]) rotate([sy*-58,0,0]) cylinder(h=0.36,r1=0.016,r2=0.008,$fn=3);
    }
}

// Pit latrine: an iron-sheet cubicle with a leaning door.
module sv_prop_latrine(seed=0)
{
    c=sv_zinc(seed);
    sv_m_concrete([0.34,0.33,0.31]) sv_slab(1.3,1.5,0.1);
    sv_m_mabati(c,0.5) { translate([0,0.72,1.1]) sv_box([1.3,0.03,2.2]); for(sx=[-1,1]) translate([sx*0.63,0,1.1]) sv_box([0.03,1.45,2.2]); translate([0,0,2.25]) rotate([-8,0,0]) sv_box([1.6,1.8,0.03]); }
    sv_m_mabati(c*0.85,0.6) translate([-0.6,-0.72,1.0]) rotate([0,0,-35]) translate([0.5,0,0]) sv_box([1.0,0.03,2.0]);
}

// Motorcycle taxi (boda boda) left on its side stand, tyres flat.
module sv_prop_boda(seed=0)
{
    rotate([sv_rnd(seed,3)==0 ? 78 : 10,0,0])
    {
        sv_m_iron([0.08,0.08,0.08]) for(x=[-0.65,0.65]) translate([x,0,0.3]) rotate([90,0,0]) cylinder(h=0.1,r=0.3,center=true,$fn=10);
        sv_m_enamel(sv_enamel(seed)) { translate([0.05,0,0.62]) scale([1.5,0.5,0.6]) sphere(r=0.24,$fn=7); translate([-0.35,0,0.72]) sv_box([0.6,0.24,0.1]); }
        sv_m_iron() { sv_rod([0.65,0,0.3],[0.45,0,0.95],0.03,4); translate([0.45,0,0.95]) rotate([90,0,0]) cylinder(h=0.6,r=0.02,center=true,$fn=4); translate([0,0,0.4]) sv_box([0.5,0.2,0.25]); }
    }
}

// ================= Flora =================
// Umbrella thorn (Vachellia tortilis): the flat-topped silhouette of the plains.
module sv_nature_acacia(seed=0,h=8)
{
    lean=sv_rf(seed,-6,6); tc=[0.24,0.20,0.16];
    rotate([lean,sv_rf(seed+1,-5,5),0])
    {
        gf_bark(tc,0.7) gf_trunk(h*0.5,h*0.035,h*0.024,flare=1.4,fn=6);
        gf_bark(tc,0.7) for(i=[0:4]) let(a=i*72+sv_rnd(seed+i,30),l=h*0.62,t=52+sv_rnd(seed+i*3,12))
            gf_limb([0,0,h*0.45],[l*sin(t)*cos(a),l*sin(t)*sin(a),h*0.45+l*cos(t)],h*0.02,h*0.006,0.06,4);
        // The umbrella: a broad, thin table of foliage, light leaking through it.
        gf_crown(seed,[0,0,h*0.9],h*0.36,[0.155,0.20,0.085],squash=0.17,n=7,spread=1.0,transmission=0.7,thickness=0.1,fn=10);
    }
}

// Yellow fever tree (Vachellia xanthophloea): luminous yellow-green bark,
// tall bare stem, thin feathery layers. Grows with its feet near water.
module sv_nature_fever(seed=0,h=14)
{
    tc=[0.30,0.32,0.14];
    gf_bark(tc,1.8) gf_trunk(h*0.62,h*0.026,h*0.016,bend=[sv_rf(seed,-0.5,0.5),sv_rf(seed+2,-0.5,0.5)],fn=6);
    gf_bark(tc,1.8) gf_limbs(seed,4,h*0.5,h*0.24,h*0.78,h*0.013,4);
    gf_crown(seed,[0,0,h*0.82],h*0.22,[0.175,0.22,0.085],squash=0.38,n=7,spread=0.95,transmission=0.7,thickness=0.1,fn=9);
}

// Sycamore fig / riverine giant: buttressed trunk, a huge dark dome.
module sv_nature_fig(seed=0,h=17)
{
    tc=[0.23,0.20,0.16];
    gf_bark(tc,0.9) { gf_trunk(h*0.38,h*0.07,h*0.04,flare=1.8,fn=8);
        for(i=[0:4]) let(a=i*72+sv_rnd(seed+i,20)) gf_limb([h*0.1*cos(a),h*0.1*sin(a),h*0.025],[h*0.02*cos(a),h*0.02*sin(a),h*0.2],h*0.03,0.02,0.05,4);
        gf_limbs(seed,4,h*0.32,h*0.28,h*0.62,h*0.026); }
    gf_crown(seed,[0,0,h*0.68],h*0.33,[0.09,0.155,0.065],squash=0.72,n=8,spread=0.72,fn=10);
}

// Desert date (Balanites): the lone round-crowned tree of the open Mara.
module sv_nature_balanites(seed=0,h=7)
{
    gf_bark([0.27,0.24,0.20]) gf_trunk(h*0.52,h*0.04,h*0.028,fn=6);
    gf_crown(seed,[0,0,h*0.74],h*0.34,[0.13,0.18,0.085],squash=0.72,n=6,spread=0.66,fn=9);
}

// Candelabra euphorbia: a trunk carrying a bowl of upright succulent arms.
module sv_nature_euphorbia(seed=0,h=7)
{
    color([0.26,0.23,0.19]) cylinder(h=h*0.42,r1=h*0.05,r2=h*0.04,$fn=6);
    for(i=[0:8]) let(a=i*40+sv_rnd(seed+i,20),d=h*(i<3 ? 0.07 : 0.2),t=i<3 ? 4 : 30)
        color(i%2==0 ? [0.11,0.17,0.10] : [0.12,0.19,0.11]) translate([0,0,h*0.4]) rotate([0,t,a]) { cylinder(h=d/sin(max(t,8))*0.5+0.1,r=h*0.02,$fn=4); translate([0,0,d/sin(max(t,8))*0.5]) rotate([0,-t,0]) cylinder(h=h*sv_rf(seed+i,0.34,0.55),r1=h*0.024,r2=h*0.018,$fn=4); }
}

// Croton / orange-leaf thicket: the grey-green scrub of ridges and luggas.
module sv_nature_croton(seed=0,r=2.4)
{
    gf_bark([0.24,0.21,0.17]) for(i=[0:2]) rotate([sv_rf(seed+i,-25,25),sv_rf(seed+i*3,-25,25),0]) cylinder(h=r*0.9,r1=0.05,r2=0.02,$fn=4);
    gf_shrub(seed,r*0.6,[0.16,0.19,0.115],n=5,spread=0.7,squash=0.85,transmission=0.45);
}

// Whistling thorn: knee-to-head-high thorn bush with swollen galls.
module sv_nature_thornbush(seed=0)
{
    gf_bark([0.27,0.24,0.19]) for(i=[0:5]) rotate([sv_rf(seed+i,18,48),0,i*60+sv_rnd(seed+i,30)]) { cylinder(h=sv_rf(seed+i*3,1.1,1.9),r1=0.03,r2=0.008,$fn=3); translate([0,0,0.8]) sphere(r=0.045,$fn=4); }
    gk_crease(70) gk_puff_normals(strength=0.6,upBias=0.3)
        for(i=[0:2]) gf_leaf(gf_ramp([0.17,0.21,0.10],0.3+i*0.3),0.55,0.1)
            translate([sv_rf(seed+i*5,-0.5,0.5),sv_rf(seed+i*7,-0.5,0.5),sv_rf(seed+i,1.0,1.4)]) scale([1,1,0.35]) sphere(r=0.55,$fn=7);
}

// Red oat grass tussock: tall, golden, seeding.
module sv_nature_grass(seed=0)
{
    for(i=[0:8]) let(a=i*40+sv_rnd(seed+i,30),t=sv_rf(seed+i*3,6,26))
        color(i%3==0 ? [0.42,0.31,0.16] : [0.44,0.38,0.19]) rotate([0,t,a]) cylinder(h=sv_rf(seed+i*5,0.8,1.5),r1=0.035,r2=0.004,$fn=3);
}

// Sisal: a rosette of sword leaves, sometimes with its flowering pole.
module sv_nature_sisal(seed=0)
{
    for(i=[0:9]) color(i%2==0 ? [0.14,0.21,0.14] : [0.17,0.23,0.15]) rotate([0,35+(i%3)*18,i*36+sv_rnd(seed+i,15)]) scale([1,0.35,1]) cylinder(h=sv_rf(seed+i,0.9,1.4),r1=0.11,r2=0.01,$fn=4);
    if(sv_rnd(seed,3)==0) color([0.33,0.30,0.20]) { cylinder(h=4.5,r1=0.05,r2=0.02,$fn=4); for(z=[3.2:0.4:4.4]) translate([0,0,z]) rotate([0,0,z*90]) sv_box([0.9,0.04,0.04]); }
}

// Banana clump: pseudo-stems and broad torn leaves by a homestead.
module sv_nature_banana(seed=0)
{
    for(k=[0:2]) let(x=sv_rf(seed+k*3,-0.6,0.6),y=sv_rf(seed+k*5,-0.6,0.6),h=sv_rf(seed+k,2.2,3.4)) translate([x,y,0])
    {
        color([0.22,0.24,0.13]) cylinder(h=h,r1=0.13,r2=0.08,$fn=6);
        for(i=[0:5]) color(i%2==0 ? [0.13,0.22,0.09] : [0.16,0.24,0.09]) translate([0,0,h]) rotate([0,55+(i%3)*14,i*60+sv_rnd(seed+i+k,30)]) translate([0,0,0.9]) scale([0.5,0.06,1.9]) sphere(r=0.5,$fn=6);
    }
}

// Dry maize stalks left standing in the shamba.
module sv_nature_maize(seed=0)
{
    for(i=[0:5]) let(x=sv_rf(seed+i*3,-0.7,0.7),y=sv_rf(seed+i*5,-0.7,0.7),h=sv_rf(seed+i*7,1.4,2.1)) translate([x,y,0]) rotate([sv_rf(seed+i,-10,10),sv_rf(seed+i*9,-10,10),0])
    {
        color([0.42,0.36,0.19]) cylinder(h=h,r1=0.025,r2=0.012,$fn=3);
        color([0.40,0.33,0.16]) for(k=[0:2]) translate([0,0,h*(0.35+k*0.2)]) rotate([0,60,k*120+i*40]) scale([1,0.25,1]) cylinder(h=0.6,r1=0.05,r2=0.005,$fn=3);
    }
}

// Tree pushed over or ring-barked by elephants: a grey skeleton.
module sv_nature_dead(seed=0,h=7)
{
    gf_bark([0.34,0.31,0.28],1.2) rotate([sv_rnd(seed,3)==0 ? 62 : sv_rf(seed,-8,8),0,sv_rnd(seed,360)])
    {
        gf_trunk(h*0.6,h*0.035,h*0.02,flare=1.3,fn=6);
        for(i=[0:3]) let(z=h*(0.4+i*0.06),a=i*90+sv_rnd(seed+i*3,40),t=44+sv_rnd(seed+i,20),l=h*0.4)
            gf_limb([0,0,z],[l*sin(t)*cos(a),l*sin(t)*sin(a),z+l*cos(t)],h*0.016,0.01,0.08,4);
    }
}

// Wild date palm of the river gallery.
module sv_nature_palm(seed=0,h=9)
{
    rotate([sv_rf(seed,-9,9),sv_rf(seed+1,-9,9),0])
    {
        gf_bark([0.26,0.22,0.17],0.5) gf_trunk(h,0.22,0.16,bend=[sv_rf(seed+2,-0.6,0.6),sv_rf(seed+3,-0.6,0.6)],flare=1.3,fn=7);
        translate([sv_rf(seed+2,-0.6,0.6),sv_rf(seed+3,-0.6,0.6),0])
            for(i=[0:8]) gf_leaf(gf_ramp([0.135,0.205,0.085],i%3==0 ? 0 : 0.5+0.5*(i%2)),0.65,0.06) translate([0,0,h]) rotate([0,48+(i%3)*22,i*40+sv_rnd(seed+i,20)]) translate([0,0,1.5]) scale([0.42,0.05,3.2]) sphere(r=0.5,$fn=6);
    }
}
