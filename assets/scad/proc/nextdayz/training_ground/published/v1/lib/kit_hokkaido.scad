// Hokkaido port-town kit. Pure definitions; hk_ namespace, metres, base z=0,
// front=-y. Otaru timber-framed stone stores, boarded houses, metal roofs,
// narrow shops and Meiji shipping offices. Openings are actual wall voids.
use <kit_coldwar.scad>

function hk_sq(x) = (x*x+x*433+71)%65521;
function hk_rnd(s,n) = hk_sq(hk_sq(((s%65521)+65521)%65521)+43)%n;
function hk_rf(s,a,b) = a+(b-a)*hk_rnd(s,1000)/999;
function hk_wall(s) = [[0.32,0.35,0.32],[0.38,0.36,0.28],[0.29,0.34,0.36],[0.35,0.29,0.23],[0.40,0.39,0.34]][hk_rnd(s,5)];
function hk_stone(s) = [[0.34,0.33,0.29],[0.39,0.37,0.32],[0.31,0.32,0.30]][hk_rnd(s,3)];
function hk_metal(s) = [[0.22,0.27,0.28],[0.25,0.17,0.12],[0.19,0.25,0.22],[0.29,0.29,0.26],[0.29,0.17,0.13]][hk_rnd(s,5)];
function hk_wood() = [0.19,0.14,0.10];
function hk_trim() = [0.44,0.43,0.37];
function hk_iron() = [0.14,0.16,0.16];
module hk_box(s) cube(s,center=true);

// Exact rectangular wall decomposition; sorted openings [x,bottom,width,height].
module hk_part_wall(L=10,H=3,ops=[[-2.5,0.9,1.4,1.3],[0,0,1.8,2.5],[2.5,0.9,1.4,1.3]],t=0.32,c=[0.32,0.35,0.32])
{
    edges=concat([-L/2],[for(o=ops) each [o[0]-o[2]/2,o[0]+o[2]/2]],[L/2]);
    color(c)
    {
        for(i=[0:len(ops)]) if(edges[2*i+1]-edges[2*i]>0.001)
            translate([(edges[2*i+1]+edges[2*i])/2,0,H/2]) hk_box([edges[2*i+1]-edges[2*i],t,H]);
        for(o=ops)
        {
            if(o[1]>0) translate([o[0],0,o[1]/2]) hk_box([o[2],t,o[1]]);
            if(H>o[1]+o[3]) translate([o[0],0,(H+o[1]+o[3])/2]) hk_box([o[2],t,H-o[1]-o[3]]);
        }
    }
}

module hk_part_window(seed=0,w=1.4,h=1.3)
{
    color(hk_trim())
    {
        for(sx=[-1,1]) translate([sx*(w/2+0.04),-0.04,0]) hk_box([0.10,0.10,h+0.18]);
        for(sz=[-1,1]) translate([0,-0.04,sz*(h/2+0.04)]) hk_box([w+0.18,0.10,0.10]);
    }
    color(hk_wood())
    {
        translate([0,0.04,0]) hk_box([0.045,0.06,h]);
        translate([0,0.04,0]) hk_box([w,0.06,0.045]);
        if(hk_rnd(seed,6)==0) for(z=[-0.3,0.3]) translate([0,-0.09,z*h]) rotate([0,8,0]) hk_box([w+0.1,0.05,0.15]);
    }
}

module hk_part_facade(seed=0,L=10,floors=2,t=0.32,c=[],door=true,shop=false,warehouse=false,boards=false)
{
    cc=len(c)>0 ? c : hk_wall(seed);
    fh=warehouse ? 5.5 : 2.9;
    n=max(3,2*floor((L-1)/5)+1);
    pitch=(L-0.8)/n;
    dw=warehouse ? min(3.2,pitch-0.25) : 1.8;
    for(f=[0:floors-1])
    {
        ops=[for(i=[0:n-1]) let(x=(i-(n-1)/2)*pitch)
            f==0 && door && i==floor(n/2) ? [x,0,dw,warehouse ? 3.5 : 2.5] :
            [x,warehouse ? 3.65 : shop && f==0 ? 0.5 : 0.85,min(pitch-0.35,shop ? 2.1 : 1.45),warehouse ? 1.0 : 1.4]];
        translate([0,0,f*fh]) hk_part_wall(L,fh,ops,t,cc);
        if(boards) for(z=[0.28:0.3:fh-0.12]) let(
            cuts=[for(o=ops) if(z>o[1]-0.03 && z<o[1]+o[3]+0.03) o],
            ed=concat([-L/2],[for(o=cuts) each [o[0]-o[2]/2,o[0]+o[2]/2]],[L/2]))
            color(cc*0.72) for(j=[0:len(cuts)]) if(ed[2*j+1]-ed[2*j]>0.01)
                translate([(ed[2*j+1]+ed[2*j])/2,-t/2-0.018,f*fh+z]) hk_box([ed[2*j+1]-ed[2*j],0.035,0.035]);
        for(o=ops) if(o[1]>0) translate([o[0],-t/2,f*fh+o[1]+o[3]/2]) hk_part_window(seed+f*71+round(o[0]*13),o[2],o[3]);
    }
    // Trim and weathering stop above the open doorway.
    color(hk_trim()) translate([0,-t/2-0.025,floors*fh-0.16]) hk_box([L,0.10,0.16]);
    if(door) color(hk_wood())
    {
        for(sx=[-1,1]) translate([sx*(dw/2+0.05),-t/2,1.2]) hk_box([0.10,0.13,2.4]);
        translate([0,-t/2,warehouse ? 3.58 : 2.57]) hk_box([dw+0.22,0.13,0.14]);
    }
}

// Closed gable wedge; standing seams lie on the metal slopes.
module hk_part_roof(seed=0,L=10,D=8,rise=1.7)
{
    cw_part_roof(L,D,rise,0.26,0,hk_metal(seed));
    a=atan2(rise,D/2+0.26);
    color(hk_metal(seed+2)) for(x=[-L/2:0.85:L/2],sy=[-1,1])
        translate([x,sy*(D/4+0.13),rise/2+0.025]) rotate([-sy*a,0,0]) hk_box([0.045,norm([D/2+0.26,rise]),0.045]);
    color(hk_iron()) translate([0,0,rise+0.035]) hk_box([L+0.55,0.16,0.12]);
}

module hk_part_shell(seed=0,L=10,D=8,floors=2,style="boards")
{
    stone=style=="stone";
    shop=style=="shop";
    flat=style=="flat";
    office=style=="office";
    warehouse=stone && floors==1;
    H=(warehouse ? 5.5 : 2.9*floors);
    t=stone || office ? 0.44 : 0.30;
    cc=stone || office ? hk_stone(seed) : hk_wall(seed);
    color([0.24,0.25,0.23]) cw_slab(L,D,0.10);
    for(sy=[-1,1]) translate([0,sy*(D/2-t/2),0]) rotate([0,0,sy<0 ? 0 : 180])
        hk_part_facade(seed+(sy+1)*37,L,floors,t,cc,sy<0 || warehouse,shop,warehouse,!stone && !office && !flat);
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90])
        hk_part_facade(seed+(sx+1)*61,D,floors,t,cc,false,false,warehouse,!stone && !office && !flat);
    if(stone || office)
    {
        color(hk_stone(seed+1)) for(sy=[-1,1],z=[0.6:0.65:H-0.2],x=[-L/2+0.3:1.7:L/2-0.2])
            if(abs(x)>2) translate([x,sy*(D/2+0.008),z]) hk_box([1.4,0.025,0.035]);
        color(hk_trim()) for(sx=[-1,1],sy=[-1,1]) translate([sx*(L/2-0.2),sy*(D/2+0.04),H/2]) hk_box([0.36,0.13,H]);
    }
    else
    {
        color(hk_wall(seed+3)) for(sy=[-1,1],z=[0.32:0.3:H-0.25])
            // Thin siding seams remain in solid piers, outside every window bay.
            for(sx=[-1,1]) translate([sx*(L/2-0.22),sy*(D/2+0.012),z]) hk_box([0.36,0.035,0.045]);
        color(hk_wood()) for(sx=[-1,1]) translate([sx*(L/2-0.08),-D/2-0.035,H/2]) hk_box([0.13,0.10,H]);
    }
    if(flat)
    {
        color(hk_metal(seed)) translate([0,0,H+0.12]) hk_box([L+0.15,D+0.15,0.24]);
        color(hk_wall(seed)) for(sy=[-1,1]) translate([0,sy*D/2,H+0.38]) hk_box([L,0.18,0.55]);
    }
    else if(style!="ruin") translate([0,0,H]) hk_part_roof(seed,L,D,warehouse ? D*0.26 : office ? D*0.19 : D*hk_rf(seed,0.16,0.26));
    else color(hk_wood()) for(x=[-L/2+0.5:1.6:L/2]) translate([x,0,H+0.08]) hk_box([0.14,D,0.16]);
    color(hk_iron()) for(sx=[-1,1]) translate([sx*(L/2-0.3),-D/2+0.05,H/2]) hk_box([0.09,0.09,H]);
    if(shop)
    {
        color(hk_metal(seed+1)) translate([0,-D/2-0.35,2.68]) rotate([9,0,0]) hk_box([L-0.5,0.7,0.10]);
        color(hk_wood()) translate([0,-D/2-0.10,3.25]) hk_box([L*0.82,0.14,0.55]);
        color(hk_trim()) for(x=[-L*0.24:0.6:L*0.24]) translate([x,-D/2-0.18,3.26]) hk_box([0.18,0.025,0.24]);
    }
    if(office)
    {
        color(hk_trim()) for(sx=[-1,1]) translate([sx*1.25,-D/2-0.10,1.5]) hk_box([0.26,0.26,3]);
        color(hk_trim()) translate([0,-D/2-0.22,3.05]) hk_box([3.1,0.45,0.3]);
        color(hk_trim()) translate([0,-D/2-0.12,H]) rotate([90,0,0]) linear_extrude(height=0.18)
            polygon([[-2.2,0],[2.2,0],[0,1.1]]);
    }
    if(style=="boards") color(hk_metal(seed+2)) translate([0,-D/2-0.35,2.6]) rotate([10,0,0]) hk_box([2.4,0.7,0.09]);
    // Side equipment and chimney never cross the central door.
    color(hk_metal(seed+4)) translate([L/2-0.7,D/2-0.7,H+0.8]) hk_box([0.38,0.40,1.6]);
    if(floors>1 && !stone) color([0.30,0.31,0.28]) translate([L*0.34,-D/2-0.22,3.55]) hk_box([0.8,0.4,0.45]);
}

module hk_bldg_house(seed=0,L=10,D=8) hk_part_shell(seed,L,D,1+hk_rnd(seed,2),"boards");
module hk_bldg_rowhouse(seed=0,L=9,D=10) hk_part_shell(seed,L,D,2,"boards");
module hk_bldg_ruin(seed=0,L=10,D=8) hk_part_shell(seed,L,D,1,"ruin");
module hk_bldg_shop(seed=0,L=9,D=11) hk_part_shell(seed,L,D,2,"shop");
module hk_bldg_warehouse(seed=0,L=20,D=13) hk_part_shell(seed,L,D,1,"stone");
module hk_bldg_workshop(seed=0,L=16,D=11) hk_part_shell(seed,L,D,1,"boards");
module hk_bldg_office(seed=0,L=18,D=12) hk_part_shell(seed,L,D,2,"office");
module hk_bldg_clinic(seed=0,L=18,D=9) hk_part_shell(seed,L,D,2,"flat");
module hk_bldg_apartment(seed=0,L=14,D=10) hk_part_shell(seed,L,D,3,"flat");
module hk_bldg_koban(seed=0,L=10,D=8) hk_part_shell(seed,L,D,1,"flat");
module hk_bldg_shrine(seed=0,L=10,D=8)
{
    hk_part_shell(seed,L,D,1,"boards");
    color(hk_wood()) for(sx=[-1,1]) translate([sx*(L/2-0.6),-D/2-0.12,1.5]) hk_box([0.20,0.22,3]);
    color(hk_metal(seed)) translate([0,-D/2-0.35,2.7]) hk_box([L,0.9,0.15]);
}

module hk_prop_torii(seed=0,a=5,b=0)
{
    color([0.30,0.15,0.10])
    {
        for(sx=[-1,1]) translate([sx*a*0.32,0,0]) cylinder(h=3.7,r1=0.18,r2=0.15,$fn=10);
        translate([0,0,3.25]) hk_box([a*0.85,0.22,0.23]);
        translate([0,0,3.9]) hk_box([a,0.36,0.27]);
    }
    color(hk_iron()) translate([0,0,4.08]) hk_box([a+0.3,0.45,0.12]);
}
module hk_prop_pole(seed=0,a=0,b=0)
{
    color([0.28,0.29,0.26]) cylinder(h=8,r1=0.15,r2=0.095,$fn=8);
    color(hk_iron()) for(z=[6.5,7.4]) translate([0,0,z]) hk_box([2.3,0.13,0.12]);
    color(hk_trim()) for(x=[-0.85,0,0.85]) translate([x,0,7.5]) cylinder(h=0.22,r=0.09,$fn=6);
}
module hk_prop_bollard(seed=0,a=0,b=0)
{
    color(hk_iron()) { cylinder(h=0.72,r=0.18,$fn=10); translate([0,0,0.52]) hk_box([0.7,0.22,0.22]); }
}
module hk_prop_vending(seed=0,a=0,b=0)
{
    color(hk_metal(seed)) translate([0,0,0.9]) hk_box([0.85,0.75,1.8]);
    color(hk_iron()) translate([0,-0.39,1.1]) hk_box([0.65,0.04,0.8]);
    color(hk_trim()) for(x=[-0.2,0,0.2],z=[0.9,1.2,1.5]) translate([x,-0.42,z]) hk_box([0.10,0.025,0.16]);
}
module hk_prop_lantern(seed=0,a=0,b=0)
{
    color(hk_stone(seed)) { cw_slab(0.7,0.7,0.15); translate([0,0,0.15]) cylinder(h=0.9,r=0.12,$fn=8); }
    color(hk_stone(seed)) translate([0,0,1.2]) hk_box([0.5,0.5,0.5]);
    translate([0,0,1.45]) cw_part_roof(0.55,0.55,0.28,0.1,0.3,hk_stone(seed));
}

// Northern broadleaf woodland: birch / oak / elm, with Ezo-spruce pockets.
module hk_nature_birch(seed=0) cw_nature_birch(s=hk_rf(seed,1.4,1.8),seed=seed);
module hk_nature_spruce(seed=0)
{
    H=hk_rf(seed,11,16);
    color([0.19,0.15,0.10]) cylinder(h=H*0.9,r1=0.23,r2=0.07,$fn=7);
    color([0.10,0.20,0.13]) for(i=[0:5]) translate([0,0,H*(0.16+i*0.12)])
        cylinder(h=H*0.34,r1=H*(0.20-i*0.022),r2=0.06,$fn=8);
}
module hk_nature_elm(seed=0)
{
    H=hk_rf(seed,9,13);
    color([0.21,0.17,0.12]) cylinder(h=H*0.72,r1=0.26,r2=0.10,$fn=7);
    color([0.21,0.30,0.13]) for(i=[0:4]) let(a=i*137+hk_rnd(seed,90))
        translate([cos(a)*H*0.16,sin(a)*H*0.16,H*(0.65+0.06*(i%3))]) scale([1.15,1,0.9]) sphere(r=H*0.24,$fn=9);
}
module hk_nature_shrub(seed=0) scale([1.1,1,0.85]) cw_nature_bush(seed=seed);
module hk_nature_alder(seed=0) scale([0.65,0.7,0.7]) hk_nature_elm(seed);
module hk_nature_oak(seed=0)
{
    color([0.20,0.16,0.11]) cylinder(h=6.5,r1=0.35,r2=0.17,$fn=8);
    color([0.22,0.28,0.10]) for(i=[0:5]) let(a=i*137+hk_rnd(seed,80))
        translate([cos(a)*2.3,sin(a)*2.3,7.5+(i%3)*1.1]) scale([1.2,1,0.8]) sphere(r=3.1,$fn=9);
}
