// Shared construction pieces for kit_raiden_frontier; no catalogue entries.
// Z-up, metres. Keep all visible surfaces opaque for the gameplay camera.
use <raiden_parts.scad>

function rf_SAND() = [0.32,0.28,0.18];
function rf_SALT() = [0.39,0.41,0.36];
function rf_SNOW() = [0.40,0.46,0.47];
function rf_ICE() = [0.15,0.32,0.38];
function rf_METAL() = [0.20,0.26,0.27];
function rf_DARK() = [0.055,0.075,0.080];
function rf_WALL() = [0.30,0.34,0.32];
function rf_OCHRE() = [0.42,0.32,0.09];
function rf_TEAL() = [0.07,0.25,0.24];
function rf_GLASS() = [0.07,0.21,0.28];
function rf_sq(x) = (x*x+x*619+97)%65521;
function rf_rnd(seed, m) = rf_sq(rf_sq(((seed%65521)+65521)%65521)+29)%m;
function rf_variant(v) = ((floor(v)%3)+3)%3;

module rf_pad(size = [8,6])
{
    color([0.22,0.25,0.23]) nr_box([size[0],size[1],0.18]);
}

// Explicit endpoints keep tapered tower bracing attached at every height.
module rf_beam(a, b, width = 0.16)
{
    hull()
    {
        translate(a) cube([width,width,width], center = true);
        translate(b) cube([width,width,width], center = true);
    }
}

module rf_pipe(a, b, radius = 0.24)
{
    d = b-a;
    translate(a) rotate([0,acos(d[2]/norm(d)),atan2(d[1],d[0])])
        cylinder(h = norm(d), r = radius, $fn = 8);
}

module rf_roof(width = 4, depth = 5, rise = 0.8)
{
    hull()
    {
        nr_box([width,depth,0.12]);
        translate([0,0,rise]) nr_box([0.16,depth,0.12]);
    }
}

module rf_hut(size = [4,5,2.2], tone = [0.30,0.34,0.32], snow = false)
{
    color(tone) nr_box(size);
    color(snow ? rf_SNOW() : rf_METAL()) translate([0,0,size[2]])
        rf_roof(size[0]+0.25,size[1]+0.25,0.6);
    color(rf_DARK()) translate([0,-size[1]/2-0.025,0]) nr_box([1.05,0.08,1.7]);
    for (s = [-1,1]) color(rf_GLASS()) translate([s*size[0]/2,0,size[2]*0.55])
        nr_box([0.045,size[1]*0.58,0.55]);
}

module rf_tower(height = 5.3, radius = 1.2)
{
    for (sx = [-1,1], sy = [-1,1])
    {
        rf_beam([sx*radius,sy*radius,0.12],[sx*0.35,sy*0.35,height],0.20);
        for (i = [0:2])
        {
            z0 = 0.15+i*(height-0.15)/3;
            z1 = 0.15+(i+1)*(height-0.15)/3;
            r0 = radius+(0.35-radius)*(z0-0.12)/(height-0.12);
            r1 = radius+(0.35-radius)*(z1-0.12)/(height-0.12);
            rf_beam([sx*r0,sy*r0,z0],[-sx*r1,sy*r1,z1],0.10);
        }
    }
    for (z = [height/3,height*2/3,height])
    {
        r = radius+(0.35-radius)*(z-0.12)/(height-0.12);
        for (s = [-1,1])
        {
            translate([s*r,0,z]) nr_box([0.15,r*2,0.12]);
            translate([0,s*r,z]) nr_box([r*2,0.15,0.12]);
        }
    }
}

module rf_wheels(length = 5, gauge = 1.8, radius = 0.38)
{
    for (x = [-gauge/2,gauge/2], y = [-length*0.34,length*0.34])
        color(rf_DARK()) translate([x,y,radius]) rotate([0,90,0])
            cylinder(h = 0.32, r = radius, center = true, $fn = 8);
}

module rf_railchassis(length = 7.2)
{
    rf_wheels(length);
    color(rf_METAL()) translate([0,0,0.55]) nr_box([2.6,length,0.35]);
    for (s = [-1,1]) color(rf_DARK()) translate([0,s*(length/2+0.18),0.56])
        nr_box([0.38,0.36,0.20]);
}

module rf_reel(radius = 0.65)
{
    color(rf_DARK()) translate([0,0,radius]) rotate([0,90,0])
        cylinder(h = 0.85, r = radius*0.66, center = true, $fn = 10);
    for (x = [-0.48,0.48]) color(rf_OCHRE()) translate([x,0,radius]) rotate([0,90,0])
        cylinder(h = 0.12, r = radius, center = true, $fn = 10);
    for (s = [-1,1]) color(rf_METAL()) translate([0,s*radius*0.76,0]) nr_box([1.2,0.20,0.30]);
}

module rf_dish(radius = 1.9, angle = 25)
{
    color(rf_METAL()) cylinder(h = 2.5, r1 = 0.42, r2 = 0.28, $fn = 8);
    translate([0,0,2.5]) rotate([angle,0,0])
    {
        color(rf_WALL()) rotate_extrude($fn = 12)
            polygon([[0,0],[radius,0.65],[radius,0.82],[0,0.17]]);
        color(rf_DARK()) cylinder(h = 1.25, r = 0.075, $fn = 6);
        for (s = [-1,1]) color(rf_METAL())
            rf_beam([s*radius*0.85,0,0.72],[0,0,1.2],0.065);
        color(rf_OCHRE()) translate([0,0,1.22]) nr_box([0.30,0.30,0.20]);
    }
}

module rf_pine(height = 3.8)
{
    color(rf_METAL()) cylinder(h = 1.2, r = 0.16, $fn = 6);
    for (i = [0:2])
    {
        z = 0.55+i*height*0.19;
        r = height*(0.29-i*0.065);
        h = height*(0.42-i*0.04);
        color([0.075,0.19,0.15]) translate([0,0,z]) cylinder(h = h, r1 = r, r2 = 0, $fn = 6);
        color(rf_SNOW()) translate([0,0,z+h*0.20])
            cylinder(h = h*0.81, r1 = r*0.82, r2 = 0, $fn = 6);
    }
}
