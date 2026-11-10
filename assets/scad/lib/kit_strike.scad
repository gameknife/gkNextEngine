// Coastal freight district. Prefix sk_; pure parts; 1 unit = 1 m.
// Grounded parts: z=0. Facades: -Y. Vehicles: +X. CSG stays inside parts.
function sk_CONCRETE() = [0.34,0.38,0.39];
function sk_LIGHT() = [0.40,0.44,0.43];
function sk_DARK() = [0.075,0.105,0.12];
function sk_TEAL() = [0.08,0.32,0.30];
function sk_RED() = [0.46,0.14,0.10];
function sk_YELLOW() = [0.69,0.43,0.08];
function sk_WOOD() = [0.31,0.23,0.14];
function sk_STEEL() = [0.20,0.25,0.27];
function sk_sq(x) = (x*x+x*419+73)%65521;
function sk_rnd(s,m) = sk_sq(sk_sq(((s%65521)+65521)%65521)+29)%m;
function sk_wall_color(i) = [sk_LIGHT(),sk_TEAL(),[0.32,0.39,0.41],sk_RED()][sk_rnd(i,4)];
module sk_box(s=[1,1,1]) { cube(s,center=true); }
module sk_slab(w=4,d=4,h=0.2) { translate([0,0,h/2]) cube([w,d,h],center=true); }

// ================= Ground and circulation =================
module sk_ground_paving(w=8,d=8,h=0.2,c=sk_CONCRETE()) { color(c) sk_slab(w,d,h); }
// Origin at bottom centre; slope rises toward +Y; top starts at 0.04 m.
module sk_ground_ramp(w=4,length=8,rise=2,c=sk_CONCRETE())
{
    color(c) polyhedron(
        points=[[-w/2,0,0],[w/2,0,0],[w/2,length,0],[-w/2,length,0],
                [-w/2,0,0.04],[w/2,0,0.04],[w/2,length,rise+0.04],[-w/2,length,rise+0.04]],
        faces=[[0,1,2,3],[4,7,6,5],[0,4,5,1],[1,5,6,2],[2,6,7,3],[3,7,4,0]]);
}
module sk_ground_stairs(w=4,run=5,rise=2,steps=12)
{
    for(i=[0:steps-1]) color(i%2==0 ? sk_CONCRETE() : [0.38,0.42,0.43])
        translate([0,(i+0.5)*run/steps,0]) sk_slab(w,run/steps,(i+1)*rise/steps);
}
module sk_ground_site(letter="A",w=8,d=7,c=sk_RED())
{
    color(c)
    {
        for(x=[-w/2,w/2]) translate([x,0,0]) sk_slab(0.12,d,0.012);
        for(y=[-d/2,d/2]) translate([0,y,0]) sk_slab(w,0.12,0.012);
        translate([-w/2+0.5,-d/2+0.5,0]) linear_extrude(height=0.014) text(letter,size=1.8);
    }
}
module sk_ground_drain(length=4)
{
    color(sk_DARK()) sk_slab(length,0.22,0.018);
    color(sk_STEEL()) for(i=[0:floor(length/0.16)])
        translate([-length/2+i*0.16,0,0.018]) sk_slab(0.045,0.22,0.012);
}

// ================= Architecture =================
module sk_bldg_wall(w=6,h=5,t=0.5,c=sk_LIGHT())
{
    gk_flatten()
    {
        color(c) sk_slab(w,t,h);
        color(sk_CONCRETE()) sk_slab(w,t+0.06,0.45);
        color(sk_LIGHT()) translate([0,0,h-0.18]) sk_slab(w+0.08,t+0.12,0.18);
        color(sk_CONCRETE()) for(x=[-w/2+0.28,w/2-0.28])
            translate([x,0,0.45]) sk_slab(0.18,t+0.045,h-0.63);
    }
}
// Fully open gateway. No threshold or invisible wall behind the leaves.
module sk_bldg_gate(w=7,clear=3.8,h=5.3,clearH=3.7,t=0.6,open=72)
{
    gk_flatten()
    {
        for(s=[-1,1]) translate([s*(w+clear)/4,0,0]) sk_bldg_wall((w-clear)/2,h,t,sk_TEAL());
        color(sk_TEAL()) translate([0,0,clearH]) sk_slab(clear,t,h-clearH);
        for(s=[-1,1]) translate([s*clear/2,0,0]) rotate([0,0,-s*open])
        {
            color(sk_WOOD()) translate([-s*clear/4,0,0]) sk_slab(clear/2,0.15,clearH-0.12);
            color(sk_DARK()) for(z=[0.38,1.7,clearH-0.45])
                translate([-s*clear/4,-0.095,z]) sk_box([clear/2,0.065,0.11]);
            color(sk_YELLOW()) translate([-s*(clear/2-0.14),-0.13,1.4]) sk_box([0.1,0.07,0.24]);
        }
    }
}
module sk_bldg_arch(w=6,h=4.8,clear=3.6,t=0.65)
{
    gk_flatten()
    {
        color(sk_LIGHT()) difference()
        {
            sk_slab(w,t,h);
            translate([0,0,-0.01]) sk_slab(clear,t+0.1,2.7);
            translate([0,0,2.65]) rotate([90,0,0]) cylinder(h=t+0.1,r=clear/2,center=true,$fn=12);
        }
        color(sk_TEAL()) for(s=[-1,1]) translate([s*(clear/2+0.17),0,0]) sk_slab(0.22,t+0.07,2.8);
    }
}
module sk_bldg_window_wall(w=10,h=6,t=0.65,sill=1.1,opening=2.6)
{
    gk_flatten()
    {
        for(s=[-1,1]) translate([s*(w+opening)/4,0,0]) sk_bldg_wall((w-opening)/2,h,t,sk_TEAL());
        color(sk_CONCRETE()) sk_slab(opening,t,sill);
        color(sk_TEAL()) translate([0,0,sill+2.3]) sk_slab(opening,t,h-sill-2.3);
        color(sk_LIGHT()) translate([0,0,sill-0.08]) sk_slab(opening+0.2,t+0.3,0.08);
    }
}

// Inaccessible infill building. Windows are relief, not entrances.
module sk_bldg_block(w=10,d=8,h=8,seed=0)
{
    gk_flatten()
    {
        color(sk_wall_color(seed)) sk_slab(w,d,h);
        color(sk_CONCRETE()) sk_slab(w+0.05,d+0.05,0.65);
        color(sk_LIGHT()) translate([0,0,h-0.23]) sk_slab(w+0.3,d+0.3,0.23);
        color(sk_STEEL()) translate([0,0,h]) sk_slab(w-0.45,d-0.45,0.12);
        for(z=[3:2.6:h-0.8],x=[-w/2+1.2:2.5:w/2-0.9])
        {
            color(sk_DARK()) translate([x,-d/2-0.025,z]) sk_box([1,0.08,1.35]);
            color(sk_TEAL()) translate([x,-d/2-0.075,z]) sk_box([0.78,0.08,1.16]);
            color(sk_LIGHT()) translate([x,-d/2-0.14,z-0.69]) sk_box([1.2,0.3,0.12]);
        }
        color(sk_STEEL()) translate([w/2-0.2,-d/2-0.10,0]) cylinder(h=h,r=0.06,$fn=8);
    }
}
module sk_bldg_awning(w=5,d=1.8)
{
    color(sk_TEAL()) translate([0,-d/2,0.20]) rotate([8,0,0]) sk_box([w,d,0.1]);
    color(sk_LIGHT()) for(x=[-w/2+0.15:0.65:w/2])
        translate([x,-d/2,0.215]) rotate([8,0,0]) sk_box([0.18,d,0.11]);
}
module sk_bldg_roof_vent()
{
    color(sk_STEEL()) sk_slab(1.2,1.2,0.6);
    color(sk_DARK()) translate([0,-0.61,0.3]) sk_box([0.94,0.02,0.42]);
    color(sk_LIGHT()) for(z=[0.14:0.10:0.5]) translate([0,-0.64,z]) sk_box([0.92,0.04,0.035]);
    color(sk_TEAL()) translate([0,0,0.6]) sk_slab(1.3,1.3,0.09);
}
module sk_bldg_tank()
{
    color(sk_STEEL()) for(x=[-0.7,0.7],y=[-0.7,0.7]) translate([x,y,0]) sk_slab(0.12,0.12,1.1);
    color(sk_TEAL()) translate([0,0,0.8]) cylinder(h=1.9,r=1.15,$fn=12);
    color(sk_LIGHT()) for(z=[0.92,2.5]) translate([0,0,z]) cylinder(h=0.1,r=1.18,$fn=12);
    color(sk_TEAL()) translate([0,0,2.7]) cylinder(h=0.35,r1=1.15,r2=0.45,$fn=12);
}

// ================= Cover and landmarks =================
module sk_prop_crate(w=1.6,d=1.5,h=1.6,c=sk_WOOD())
{
    gk_flatten()
    {
        color(c) sk_slab(w,d,h);
        color(sk_STEEL()) for(x=[-w*0.36,w*0.36]) translate([x,0,0]) sk_slab(0.075,d+0.025,h+0.015);
        color(sk_LIGHT()) translate([0,-d/2-0.014,h*0.63]) sk_box([w*0.42,0.026,0.32]);
        color(sk_DARK()) for(x=[-0.16,0,0.16]) translate([x,-d/2-0.031,h*0.63]) sk_box([0.045,0.012,0.21]);
    }
}
module sk_prop_cargo(w=3,d=2.4,h=2.7,c=sk_TEAL())
{
    gk_flatten()
    {
        color(c) sk_slab(w,d,h);
        color(sk_STEEL()) for(x=[-w/2,w/2]) translate([x,0,0]) sk_slab(0.10,d+0.06,h+0.05);
        color(sk_LIGHT()) for(z=[0.12,h-0.12]) translate([0,0,z]) sk_box([w+0.10,d+0.10,0.1]);
        color(c) for(x=[-w/2+0.2:0.27:w/2-0.1]) translate([x,-d/2-0.03,h/2]) sk_box([0.06,0.09,h-0.3]);
        color(sk_LIGHT()) translate([0,-d/2-0.09,h*0.66]) sk_box([0.85,0.03,0.38]);
    }
}
module sk_prop_barrel(c=sk_RED())
{
    color(c) cylinder(h=0.92,r=0.32,$fn=12);
    color(sk_STEEL()) for(z=[0.06,0.30,0.64,0.87]) translate([0,0,z]) cylinder(h=0.04,r=0.335,$fn=12);
    color(sk_DARK()) translate([0.12,0,0.92]) cylinder(h=0.014,r=0.045,$fn=8);
}
module sk_prop_pallet()
{
    color(sk_WOOD())
    {
        for(x=[-0.45,0,0.45]) translate([x,0,0]) sk_slab(0.12,0.9,0.10);
        for(y=[-0.4:0.2:0.4]) translate([0,y,0.10]) sk_slab(1.2,0.14,0.06);
    }
}
module sk_prop_barrier(w=3)
{
    color(sk_CONCRETE()) hull()
    {
        sk_slab(w,0.75,0.15);
        translate([0,0,0.85]) sk_slab(w,0.28,0.15);
    }
    color(sk_YELLOW()) translate([0,-0.18,0.76]) sk_box([w-0.2,0.04,0.16]);
}
module sk_prop_sign(label="A",w=1.3,c=sk_RED())
{
    color(c) sk_slab(w,0.08,0.85);
    color(sk_LIGHT()) translate([-w*0.35,-0.052,0.18]) rotate([90,0,0]) linear_extrude(height=0.014) text(label,size=0.55);
}
module sk_prop_lamp()
{
    color(sk_STEEL()) cylinder(h=4.5,r=0.07,$fn=8);
    color(sk_STEEL()) translate([0,-0.5,4.48]) sk_box([0.12,1.0,0.12]);
    color(sk_YELLOW()) translate([0,-0.8,4.40]) sk_box([0.38,0.52,0.12]);
}
module sk_prop_cable(a=[0,0,5],b=[8,0,5],sag=0.7)
{
    color(sk_DARK()) for(i=[0:11])
    {
        t=i/12; u=(i+1)/12;
        p=[a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,a[2]+(b[2]-a[2])*t-4*sag*t*(1-t)];
        q=[a[0]+(b[0]-a[0])*u,a[1]+(b[1]-a[1])*u,a[2]+(b[2]-a[2])*u-4*sag*u*(1-u)];
        hull() { translate(p) sphere(r=0.025,$fn=6); translate(q) sphere(r=0.025,$fn=6); }
    }
}
module sk_veh_hatchback(c=sk_RED())
{
    gk_flatten()
    {
        color(c) translate([0,0,0.40]) sk_slab(3.9,1.75,0.55);
        color(c) hull()
        {
            translate([-0.35,0,0.93]) sk_box([2.2,1.62,0.06]);
            translate([-0.55,0,1.55]) sk_box([1.25,1.42,0.06]);
        }
        color([0.10,0.23,0.28]) for(s=[-1,1]) translate([-0.5,s*0.771,1.2]) sk_box([1.3,0.035,0.35]);
        for(x=[-1.22,1.2],y=[-0.84,0.84])
        {
            color(sk_DARK()) translate([x,y,0.37]) rotate([90,0,0]) cylinder(h=0.23,r=0.37,center=true,$fn=12);
            color(sk_STEEL()) translate([x,y*1.14,0.37]) rotate([90,0,0]) cylinder(h=0.04,r=0.19,center=true,$fn=8);
        }
        color(sk_LIGHT()) for(y=[-0.58,0.58]) translate([1.96,y,0.73]) sk_box([0.04,0.34,0.18]);
        color(sk_DARK()) translate([1.98,0,0.49]) sk_box([0.08,1.68,0.12]);
    }
}
module sk_nature_planter()
{
    color(sk_CONCRETE()) cylinder(h=0.5,r1=0.48,r2=0.62,$fn=8);
    color([0.13,0.23,0.10]) translate([0,0,0.9]) scale([0.7,0.7,0.6]) sphere(r=1,$fn=8);
}
module sk_nature_cypress(h=6)
{
    color(sk_WOOD()) cylinder(h=h*0.45,r=0.17,$fn=8);
    for(i=[0:2]) color([0.10+i*0.02,0.22+i*0.025,0.13])
        translate([0,0,h*(0.2+i*0.18)]) cylinder(h=h*0.44,r1=h*(0.19-i*0.035),r2=0.02,$fn=7);
}

// ================= Facade details: anchor on a wall, front -Y =================
module sk_bldg_shutter(w=3.6,h=3.2,c=sk_TEAL())
{
    gk_flatten()
    {
        color(sk_DARK()) sk_slab(w+0.22,0.14,h+0.15);
        color(c) translate([0,-0.08,0.04]) sk_slab(w,0.08,h-0.04);
        color(sk_STEEL()) for(z=[0.1:0.15:h]) translate([0,-0.13,z]) sk_box([w,0.05,0.035]);
        color(sk_LIGHT()) for(s=[-1,1]) translate([s*(w/2+0.12),0,0]) sk_slab(0.16,0.28,h+0.28);
        color(sk_LIGHT()) translate([0,0,h]) sk_slab(w+0.4,0.33,0.28);
        color(sk_DARK()) translate([0,-0.16,0.18]) sk_box([0.40,0.025,0.09]);
    }
}
module sk_bldg_window(w=1.3,h=1.8)
{
    gk_flatten()
    {
        color(sk_LIGHT()) sk_slab(w+0.22,0.13,h+0.2);
        color(sk_DARK()) translate([0,-0.08,0.1]) sk_slab(w,0.08,h);
        color(sk_TEAL()) for(z=[0.16:0.16:h]) translate([0,-0.145,z]) sk_box([w-0.08,0.10,0.075]);
        color(sk_LIGHT()) translate([0,-0.15,0]) sk_slab(w+0.35,0.42,0.10);
        color(sk_LIGHT()) translate([0,-0.17,h/2]) sk_box([0.055,0.08,h]);
    }
}
module sk_prop_electrical()
{
    color(sk_STEEL()) sk_slab(0.62,0.24,0.86);
    color(sk_TEAL()) translate([0,-0.14,0.44]) sk_box([0.50,0.07,0.65]);
    color(sk_YELLOW()) translate([0,-0.185,0.52]) rotate([90,0,0])
        linear_extrude(height=0.008) polygon(points=[[-0.11,0],[0.11,0],[0,0.18]]);
    color(sk_DARK()) translate([0.19,-0.19,0.36]) sk_box([0.03,0.025,0.08]);
}
module sk_prop_downpipe(h=5)
{
    color(sk_TEAL()) cylinder(h=h,r=0.065,$fn=8);
    color(sk_STEEL()) for(z=[0.3:1.2:h]) translate([0,0,z]) sk_slab(0.20,0.16,0.06);
}
module sk_ground_edge(length=8,w=0.6)
{
    for(i=[0:floor(length/0.4)-1])
        color(i%3==0 ? [0.24,0.29,0.28] : [0.32,0.36,0.34])
            translate([-length/2+(i+0.5)*0.4,0,0]) sk_slab(0.385,w,0.035);
}
