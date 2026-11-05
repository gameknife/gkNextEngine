// kit_abandoned.scad — regional post-apocalyptic architecture, metres, front=-y.
// Pure kit: definitions only. az_ namespace; bottom z=0; deterministic weathering.
// Buildings keep real door/window voids. Detail lives in stonework, roof tiles,
// exposed framing, shutters, water stains and debris, rather than flat printed walls.
use <kit_coldwar.scad>

function az_sq(x) = (x*x + x*367 + 61) % 65521;
function az_rnd(s, n) = az_sq(az_sq(((s % 65521)+65521)%65521)+29) % n;
function az_rf(s, a, b) = a + (b-a)*az_rnd(s,1000)/999;
function az_stone(s) = [[0.37,0.38,0.35],[0.44,0.43,0.37],[0.33,0.35,0.34],[0.47,0.45,0.39]][az_rnd(s,4)];
function az_plaster(s) = [[0.45,0.38,0.27],[0.48,0.42,0.32],[0.38,0.43,0.36],[0.45,0.34,0.29],[0.47,0.46,0.39]][az_rnd(s,5)];
function az_roof(s) = [[0.31,0.16,0.12],[0.38,0.22,0.15],[0.22,0.25,0.25],[0.28,0.29,0.25]][az_rnd(s,4)];

module az_part_window(seed=0, w=1.15, h=1.45)
{
    // No solid black slab: broken glazing really opens into the room.
    color([0.24,0.21,0.15])
    {
        for (sx=[-1,1]) translate([sx*(w/2+0.045),0,0]) cube([0.09,0.14,h+0.18],center=true);
        for (sz=[-1,1]) translate([0,0,sz*(h/2+0.045)]) cube([w,0.14,0.09],center=true);
        translate([0,0,0]) cube([0.05,0.08,h],center=true);
    }
    color([0.48,0.46,0.39]) translate([0,-0.09,-h/2-0.10]) cube([w+0.30,0.30,0.12],center=true);
    if (az_rnd(seed,4)==0)
        for (i=[0:1]) color([0.28+i*0.035,0.21+i*0.025,0.14])
            translate([0,-0.12,(i-0.5)*0.40]) rotate([0,az_rf(seed+i, -13,13),0])
                cube([w+0.25,0.06,0.17],center=true);
    if (az_rnd(seed+9,3)==0)
        color([0.17,0.25,0.24]) translate([-w*0.31,0.025,-h*0.25])
            cube([w*0.31,0.025,h*0.43],center=true);
}

module az_part_wall(seed=0, L=10, h=6.4, t=0.32, door=true, stone=false, floors=2)
{
    c=stone ? az_stone(seed) : az_plaster(seed);
    dw=stone ? 3.2 : 1.8;
    dh=stone ? 3.4 : 2.5;
    cols=max(2,floor(L/2.6));
    step=L/cols;
    color(c)
    {
        // Masonry bands around actual openings, assembled without CSG.
        for (i=[0:cols])
        {
            x=-L/2+i*step;
            bottom=door && abs(x)<dw/2 ? dh : 0;
            translate([x,0,(h+bottom)/2]) cube([i==0 || i==cols ? t : 0.38,t,h-bottom],center=true);
        }
        for (floor=[0:floors-1])
        {
            z0=floor*h/floors;
            for (i=[0:cols-1])
            {
                x=-L/2+(i+0.5)*step;
                nearDoor=door && abs(x)<dw/2+step*0.45 && floor==0;
                if (!nearDoor)
                    translate([x,0,z0+0.57]) cube([step,t,1.14],center=true);
                translate([x,0,z0+h/floors-0.43]) cube([step,t,0.86],center=true);
            }
        }
    }
    // Replace central front section with a clear, generously sized entrance.
    if (door)
    {
        color(c) for(sx=[-1,1])
            translate([sx*(dw/2+0.10),0,dh/2]) cube([0.20,t+0.10,dh],center=true);
        color(stone ? [0.27,0.29,0.27] : [0.35,0.31,0.24])
            translate([0,-0.08,dh+0.12]) cube([dw+0.65,t+0.2,0.24],center=true);
    }
    for (floor=[0:floors-1],i=[0:cols-1])
    {
        x=-L/2+(i+0.5)*step;
        if (!(door && abs(x)<dw/2+step*0.45 && floor==0))
            translate([x,-t/2-0.02,floor*h/floors+(1.14+h/floors-0.86)/2])
                az_part_window(seed+i*17+floor*91,step-0.45,h/floors-2.0);
    }
    // Stone courses and irregular patches are small, separated pieces.
    for (row=[0:floor(h/0.48)-1],i=[0:cols*2-1])
        if (az_rnd(seed+row*41+i*13,stone ? 3 : 7)==0)
        {
            x=-L/2+(i+0.5)*L/(cols*2);
            z=0.25+row*0.48;
            if (!(door && abs(x)<dw/2+0.18 && z<dh))
                color(stone ? az_stone(seed+row+i) : [0.27,0.26,0.22])
                    translate([x,-t/2-0.018,z]) cube([L/(cols*2)*0.78,0.032,0.16],center=true);
        }
    for (i=[0:cols-1])
        if (az_rnd(seed+i*29,3)!=0)
            color([0.22,0.25,0.19]) translate([-L/2+(i+0.43)*step,-t/2-0.024,h-0.5])
                cube([0.12+az_rf(seed+i,0,0.25),0.035,0.8],center=true);
}

module az_part_roof(seed=0,L=10,D=8,h=2.2,metal=false)
{
    rise=atan2(h,D/2);
    slope=sqrt(h*h+D*D/4);
    tileRows=max(4,ceil(slope/0.50));
    tileCols=max(5,ceil(L/0.85));
    for (sy=[-1,1])
    {
        // Surviving rafters show through missing panels; no solid cap under damage.
        color([0.19,0.16,0.12]) for(i=[0:floor(L/1.4)])
            translate([-L/2+i*L/max(1,floor(L/1.4)),sy*D/4,h/2]) rotate([-sy*rise,0,0])
                cube([0.14,slope,0.16],center=true);
        for (row=[0:tileRows-1],col=[0:tileCols-1])
            if (az_rnd(seed+col*37+row*71+(sy+1)*51,17)>1)
                color(metal ? [0.27+az_rf(seed+col,0,0.1),0.27,0.23] : az_roof(seed+col+row))
                    translate([-L/2+(col+0.5)*L/tileCols,sy*(D/2)*(row+0.5)/tileRows,h*(1-(row+0.5)/tileRows)])
                        rotate([-sy*rise,0,0]) cube([L/tileCols+0.025,slope/tileRows+0.045,metal ? 0.05 : 0.09],center=true);
    }
    color(az_roof(seed)) translate([0,0,h+0.08]) rotate([0,90,0]) cylinder(h=L+0.1,r=0.14,center=true,$fn=12);
}

module az_prop_decay(seed=0,L=9,D=7)
{
    // Keep the entrance lane clear. Debris remains within the building's plot.
    for(i=[0:8])
    {
        sx=az_rnd(seed+i*23,2)==0 ? -1 : 1;
        x=sx*az_rf(seed+i*11,L*0.32,L*0.46);
        y=az_rf(seed+i*19,-D*0.43,D*0.43);
        color(az_stone(seed+i)) translate([x,y,0.19]) rotate([8,az_rf(seed+i,-20,20),az_rnd(seed+i*7,180)])
            cube([az_rf(seed+i,0.18,0.50),0.22,0.18],center=true);
    }
    for(i=[0:5])
    {
        x=(i%2==0 ? -1 : 1)*L*0.46;
        y=az_rf(seed+i*17,-D*0.38,D*0.42);
        color([0.22,0.29,0.13]) translate([x,y,0.2]) rotate([0,0,az_rnd(seed+i,180)])
            for(a=[-40,0,40]) rotate([0,a,0]) cylinder(h=az_rf(seed+i,0.4,0.8),r1=0.07,r2=0.01,$fn=6);
    }
}

module az_bldg_loket_townhouse(seed=0,L=10,D=8,floors=2)
{
    h=floors*3.25;
    color([0.31,0.30,0.26]) cw_slab(L,D,0.12);
    translate([0,-D/2+0.18,0]) az_part_wall(seed,L,h,0.36,true,false,floors);
    translate([0,D/2-0.18,0]) rotate([0,0,180]) az_part_wall(seed+91,L,h,0.36,false,false,floors);
    for(sx=[-1,1]) translate([sx*(L/2-0.18),0,0]) rotate([0,0,sx*90])
        az_part_wall(seed+sx*31+121,D,h,0.36,false,false,floors);
    if(floors>1) color([0.25,0.23,0.19]) translate([0,0,3.25]) cw_slab(L-0.6,D-0.6,0.13);
    color([0.36,0.34,0.29]) for(z=[0.45,h-0.20]) translate([0,0,z])
    {
        for(sy=[-1,1])
            if(sy<0 && z<2.5)
                for(sx=[-1,1]) translate([sx*(L+2.2)/4,sy*(D/2+0.01),0]) cube([(L-2.2)/2,0.14,0.19],center=true);
            else translate([0,sy*(D/2+0.01),0]) cube([L,0.14,0.19],center=true);
        for(sx=[-1,1]) translate([sx*(L/2+0.01),0,0]) cube([0.14,D,0.19],center=true);
    }
    translate([0,0,h]) az_part_roof(seed,L+0.2,D+0.2,D*0.27);
    color([0.35,0.21,0.15]) translate([L*0.28,D*0.14,h+0.4]) cw_slab(0.75,0.75,2.2);
    translate([0,0,0.14]) az_prop_decay(seed,L-0.4,D-0.4);
}

// Bohemian silhouettes: variation is structural, rather than only facade tint.
module az_bldg_loket_cottage(seed=0,L=9,D=7)
{
    h=3.4;
    color([0.28,0.28,0.23]) cw_slab(L,D,0.12);
    translate([0,-D/2+0.16,0]) az_part_wall(seed,L,h,0.32,true,false,1);
    translate([0,D/2-0.16,0]) rotate([0,0,180]) az_part_wall(seed+23,L,h,0.32,false,false,1);
    for(sx=[-1,1]) translate([sx*(L/2-0.16),0,0]) rotate([0,0,sx*90]) az_part_wall(seed+71,D,h,0.32,false,false,1);
    color([0.21,0.17,0.12]) for(x=[-L/2+0.2:1.8:L/2]) if(abs(x)>1.1) translate([x,-D/2+0.01,h/2]) cube([0.14,0.12,h],center=true);
    color([0.24,0.19,0.13]) for(sx=[-1,1]) translate([sx*(L+2.2)/4,-D/2+0.01,1.05]) cube([(L-2.2)/2,0.12,0.14],center=true);
    translate([0,0,h]) az_part_roof(seed,L,D,D*0.42);
    color([0.33,0.24,0.17]) translate([L*0.3,D*0.15,h+0.3]) cw_slab(0.65,0.65,2.0);
    az_prop_decay(seed,L-0.4,D-0.4);
}

module az_bldg_loket_shop(seed=0,L=12,D=8)
{
    h=7.1;
    color([0.29,0.28,0.24]) cw_slab(L,D,0.12);
    translate([0,-D/2+0.2,0]) az_part_wall(seed,L,h,0.4,true,false,2);
    translate([0,D/2-0.2,0]) rotate([0,0,180]) az_part_wall(seed+51,L,h,0.4,false,false,2);
    for(sx=[-1,1]) translate([sx*(L/2-0.2),0,0]) rotate([0,0,sx*90]) az_part_wall(seed+133,D,h,0.4,false,false,2);
    color([0.20,0.27,0.22]) translate([0,-D/2+0.48,2.85]) rotate([9,0,0]) cube([L*0.88,0.8,0.10],center=true);
    color([0.27,0.20,0.14]) translate([0,-D/2+0.01,3.35]) cube([L*0.66,0.12,0.42],center=true);
    color([0.38,0.36,0.29]) translate([0,0,3.65]) cw_slab(L-0.5,D-0.5,0.14);
    translate([0,0,h]) az_part_roof(seed,L,D,D*0.20);
    az_prop_decay(seed,L-0.4,D-0.4);
}

module az_bldg_loket_workshop(seed=0,L=16,D=9)
{
    h=4.1;
    color([0.27,0.27,0.23]) cw_slab(L,D,0.12);
    for(sy=[-1,1]) translate([0,sy*(D/2-0.22),0]) rotate([0,0,sy<0 ? 0 : 180]) az_part_wall(seed+sy*17+19,L,h,0.44,true,true,1);
    for(sx=[-1,1]) translate([sx*(L/2-0.22),0,0]) rotate([0,0,sx*90]) az_part_wall(seed+41,D,h,0.44,false,true,1);
    translate([0,0,h]) az_part_roof(seed,L,D,D*0.14,true);
    color([0.29,0.20,0.14]) for(x=[-L*0.35,L*0.35]) translate([x,D*0.1,0.12]) cw_slab(1.1,1.4,0.55);
    az_prop_decay(seed,L-0.4,D-0.4);
}

module az_bldg_loket_villa(seed=0,L=13,D=11)
{
    // Narrow main body and a taller square bay create an asymmetric roofline.
    az_bldg_loket_townhouse(seed,L*0.78,D,2);
    bw=min(L*0.28,D*0.42);
    translate([L/2-bw/2,-D/2+bw/2,0])
    {
        color(az_plaster(seed+43)) for(sx=[-1,1]) translate([sx*(bw/2-0.15),0,4.25]) cube([0.3,bw,8.5],center=true);
        translate([0,-bw/2+0.15,0]) az_part_wall(seed+67,bw,8.5,0.3,false,false,2);
        translate([0,0,8.5]) az_part_roof(seed,bw,bw,bw*0.65);
    }
}

module az_bldg_loket_ruin(seed=0,L=10,D=8)
{
    color([0.28,0.27,0.24]) cw_slab(L,D,0.12);
    translate([0,-D/2+0.18,0]) az_part_wall(seed,L,3.2,0.36,true,true,1);
    translate([0,D/2-0.18,0]) rotate([0,0,180]) az_part_wall(seed+21,L,2.8,0.36,false,true,1);
    for(sx=[-1,1]) translate([sx*(L/2-0.18),0,0]) rotate([0,0,sx*90]) az_part_wall(seed+43,D,3.2,0.36,false,true,1);
    color([0.17,0.15,0.12]) for(x=[-L/2+0.5:1.6:L*0.15])
        translate([x,0,3.2]) rotate([25,0,0]) cube([0.14,D*0.85,0.18],center=true);
    for(i=[0:2]) translate([L*0.25-i*0.4,D*0.22,0]) az_prop_decay(seed+i*17,L*0.4,D*0.4);
}

module az_bldg_otaru_warehouse(seed=0,L=18,D=14)
{
    h=6.2;
    color([0.29,0.29,0.26]) cw_slab(L,D,0.12);
    translate([0,-D/2+0.22,0]) az_part_wall(seed,L,h,0.44,true,true,1);
    translate([0,D/2-0.22,0]) rotate([0,0,180]) az_part_wall(seed+51,L,h,0.44,true,true,1);
    for(sx=[-1,1]) translate([sx*(L/2-0.22),0,0]) rotate([0,0,sx*90])
        az_part_wall(seed+121+sx*7,D,h,0.44,false,true,1);
    for(sy=[-1,1])
    {
        color([0.25,0.24,0.20]) translate([0,sy*D/2,h-0.2]) cube([L,0.25,0.32],center=true);
        color([0.31,0.19,0.12]) translate([-L*0.26,sy*(D/2+0.025),2.6])
            rotate([0,0,sy<0 ? 0 : 180]) for(i=[0:7])
                translate([i*0.16,0,0]) cube([0.11,0.08,3.2],center=true);
    }
    // Timber trusses survive under the partly collapsed sheet-metal roof.
    color([0.22,0.19,0.13]) for(y=[-D/2+1.5:3.0:D/2-1])
        translate([0,y,h-0.3]) cube([L-0.4,0.18,0.24],center=true);
    translate([0,0,h]) az_part_roof(seed,L+0.2,D+0.2,min(3.8,D*0.22),true);
    translate([0,0,0.14]) az_prop_decay(seed,L-0.4,D-0.4);
}

module az_bldg_otaru_rowhouse(seed=0,L=9,D=7)
{
    h=5.8;
    color([0.29,0.29,0.26]) cw_slab(L,D,0.12);
    translate([0,-D/2+0.14,0]) az_part_wall(seed,L,h,0.28,true,false,2);
    translate([0,D/2-0.14,0]) rotate([0,0,180]) az_part_wall(seed+33,L,h,0.28,false,false,2);
    for(sx=[-1,1]) translate([sx*(L/2-0.14),0,0]) rotate([0,0,sx*90])
        az_part_wall(seed+77+sx*3,D,h,0.28,false,false,2);
    color([0.24,0.20,0.15]) for(x=[-L/2+0.2:1.5:L/2])
        if(abs(x)>1.1) translate([x,-D/2-0.025,h/2]) cube([0.12,0.14,h],center=true);
    color([0.29,0.32,0.30]) translate([0,-D/2-0.025,2.8]) cube([L,0.16,0.3],center=true);
    translate([0,0,h]) az_part_roof(seed,L+0.15,D+0.15,D*0.25,true);
    translate([0,0,0.14]) az_prop_decay(seed,L-0.4,D-0.4);
}

// Hokkaido street families share front=-y and keep the entrance clear of trim.
module az_bldg_otaru_timber(seed=0,L=11,D=8,ruined=false)
{
    h=3.2;
    color([0.26,0.25,0.21]) cw_slab(L,D,0.12);
    translate([0,-D/2+0.16,0]) az_part_wall(seed,L,h,0.32,true,false,1);
    translate([0,D/2-0.16,0]) rotate([0,0,180]) az_part_wall(seed+33,L,ruined ? 2.4 : h,0.32,false,false,1);
    for(sx=[-1,1]) translate([sx*(L/2-0.16),0,0]) rotate([0,0,sx*90]) az_part_wall(seed+51,D,h,0.32,false,false,1);
    color([0.22,0.18,0.13]) for(z=[0.5:0.32:2.6],sy=[-1,1])
        if(sy<0 && z<2.5)
            for(sx=[-1,1]) translate([sx*(L+2.2)/4,-D/2-0.01,z]) cube([(L-2.2)/2,0.08,0.09],center=true);
        else translate([0,sy*(D/2+0.01),z]) cube([L,0.08,0.09],center=true);
    if(!ruined) translate([0,0,h]) az_part_roof(seed,L+0.2,D+0.2,D*0.22,true);
    else color([0.18,0.15,0.11]) for(x=[-L/2+0.5:1.5:0]) translate([x,0,h]) rotate([18,0,0]) cube([0.14,D,0.18],center=true);
    az_prop_decay(seed,L-0.4,D-0.4);
}

module az_bldg_otaru_shophouse(seed=0,L=14,D=10)
{
    az_bldg_otaru_rowhouse(seed,L,D);
    // Folded awning and faded fascia sit above the walk-in opening.
    color([0.20,0.28,0.25]) translate([0,-D/2-0.25,2.8]) rotate([12,0,0]) cube([L*0.8,0.85,0.10],center=true);
    color([0.31,0.26,0.18]) translate([0,-D/2-0.05,3.4]) cube([L*0.7,0.14,0.5],center=true);
    for(sx=[-1,1]) color([0.22,0.24,0.23]) translate([sx*(L/2-0.8),-D/2-0.16,1.5]) cube([0.55,0.15,2.3],center=true);
}

module az_bldg_otaru_apartment(seed=0,L=20,D=11)
{
    floors=3+az_rnd(seed,2);
    cw_bldg_panel_flat(seed,floors,L,D);
    // Roof tank, corroded equipment and stained elevations distinguish the port flats.
    color([0.31,0.33,0.30]) translate([-L*0.22,D*0.12,floors*2.8+1.1]) cylinder(h=1.5,r=0.8,$fn=12);
    for(f=[1:floors-1],sx=[-1,1])
        color([0.29,0.30,0.27]) translate([sx*(L/2-0.65),D/2+0.18,f*2.8+1]) cube([0.8,0.32,0.48],center=true);
    for(i=[0:6]) color([0.22,0.25,0.20]) translate([-L/2+0.8+i*(L-1.6)/6,D/2+0.02,floors*2.8-1]) cube([0.16,0.045,1.2],center=true);
    az_prop_decay(seed,L-0.4,D-0.4);
}

module az_bldg_otaru_workshop(seed=0,L=20,D=12)
{
    az_bldg_loket_workshop(seed,L,D);
    color([0.28,0.20,0.13]) for(x=[-L*0.4:1.1:L*0.4])
        translate([x,D/2+0.02,2.7]) cube([0.65,0.07,1.3],center=true);
    color([0.25,0.26,0.24]) translate([L*0.36,D*0.2,4.1]) cylinder(h=2.8,r=0.25,$fn=10);
}

module az_bldg_otaru_ruin(seed=0,L=12,D=9)
{
    az_bldg_otaru_timber(seed,L,D,true);
}

module az_bldg_castle_keep(seed=0,L=20,D=18)
{
    // A readable Bohemian hilltop landmark, with an open courtyard entrance.
    for(sx=[-1,1]) translate([sx*(L/2-2),0,0])
        color(az_stone(seed+sx)) cw_slab(3.7,D,11);
    translate([0,D/2-2,0]) color(az_stone(seed+7)) cw_slab(L,3.7,11);
    for(sx=[-1,1]) translate([sx*(L/2-2),-D/2+2,0])
    {
        color(az_stone(seed+sx+11)) cw_slab(4.0,4.0,17);
        translate([0,0,17]) az_part_roof(seed+sx,4.4,4.4,4.2);
    }
    translate([0,0,0.12]) az_prop_decay(seed,L-4,D-4);
}

module az_prop_helipad(seed=0,L=21)
{
    color([0.25,0.27,0.25]) cw_slab(L,L,0.12);
    color([0.59,0.57,0.39]) translate([0,0,0.125])
    {
        for(sx=[-1,1]) translate([sx*1.7,0,0]) cw_slab(0.55,5.5,0.015);
        cw_slab(3.4,0.60,0.015);
        difference()
        {
            cylinder(h=0.018,r=L*0.39,$fn=64);
            translate([0,0,-0.01]) cylinder(h=0.05,r=L*0.39-0.24,$fn=64);
        }
    }
    for(sx=[-1,1],sy=[-1,1]) color([0.43,0.29,0.13])
        translate([sx*L*0.45,sy*L*0.45,0.14]) cylinder(h=0.16,r=0.16,$fn=12);
}

module az_nature_thicket(seed=0,r=1.2)
{
    for(i=[0:9])
        color([0.18+az_rf(seed+i,0,0.08),0.26+az_rf(seed+i*3,0,0.09),0.12])
            translate([az_rf(seed+i*7,-r,r),az_rf(seed+i*13,-r,r),0])
                rotate([az_rf(seed+i,-18,18),az_rf(seed+i*9,-18,18),az_rnd(seed+i*11,360)])
                    cylinder(h=az_rf(seed+i*17,0.5,1.5),r1=0.10,r2=0.02,$fn=6);
}

// Bridge runs along +x. rise is the far-bank height minus near-bank height.
module az_prop_bridge(seed=0,L=35,W=7,rise=0,pierDepth=12)
{
    angle=atan2(rise,L);
    color([0.34,0.35,0.31]) rotate([0,-angle,0])
    {
        translate([0,0,-0.28]) cube([sqrt(L*L+rise*rise),W,0.56],center=true);
        for(sy=[-1,1])
        {
            translate([0,sy*(W/2-0.14),0.28]) cube([L,0.28,0.56],center=true);
            color([0.21,0.22,0.20]) translate([0,sy*(W/2-0.14),1.04]) cube([L,0.07,0.08],center=true);
            for(x=[-L/2+0.7:2.4:L/2]) translate([x,sy*(W/2-0.14),0.72])
                cube([0.08,0.08,0.7],center=true);
        }
        color([0.45,0.43,0.33]) translate([0,0,0.008]) cube([L*0.96,0.12,0.016],center=true);
    }
    color(az_stone(seed)) for(x=[-L*0.32,L*0.32],sy=[-1,1])
        translate([x,sy*W*0.30,-pierDepth/2+x*rise/L-0.3])
            cube([1.2,1.2,pierDepth],center=true);
}
