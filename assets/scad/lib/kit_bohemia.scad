// kit_bohemia.scad — West Bohemian (Egerland / Karlovy Vary hills) architecture,
// village furniture and Central European flora for abandoned NextDayz maps.
// Pure kit: definitions only. bh_ namespace; metres; base z=0; front=-y.
// Openings are real voids. Regional character lives in the silhouette: steep
// red-tile roofs (often half-hipped), folk-baroque and Egerland timber gables
// turned to the street, walled farmyards with arched gates, pastel burgher
// fronts, onion-domed towers, JZD cowsheds and weekend chaty.

// ================= PRNG / palette (zero-arg functions only) =================
function bh_sq(x) = (x*x + x*409 + 53) % 65521;
function bh_rnd(s,n) = bh_sq(bh_sq(((s % 65521)+65521)%65521)+17) % n;
function bh_rf(s,a,b) = a + (b-a)*bh_rnd(s,1000)/999;

// Path tracing lifts albedo: 0.5 already reads white, so pastels stay below it.
function bh_plaster(s) = [[0.47,0.38,0.20],[0.46,0.32,0.25],[0.36,0.40,0.31],[0.46,0.43,0.35],
                          [0.35,0.38,0.40],[0.44,0.42,0.38],[0.45,0.35,0.22],[0.41,0.30,0.30]][bh_rnd(s,8)];
function bh_trim(s) = [[0.50,0.48,0.43],[0.47,0.46,0.41],[0.44,0.42,0.37]][bh_rnd(s,3)];
function bh_tile(s) = [[0.31,0.13,0.08],[0.29,0.13,0.09],[0.33,0.15,0.10],[0.30,0.14,0.10],[0.27,0.12,0.08]][bh_rnd(s,5)];
function bh_moss(s) = [[0.19,0.21,0.11],[0.22,0.22,0.13],[0.17,0.19,0.11]][bh_rnd(s,3)];
function bh_slate(s) = [[0.19,0.20,0.21],[0.23,0.23,0.24],[0.21,0.22,0.22]][bh_rnd(s,3)];
function bh_beam(s) = [[0.20,0.10,0.07],[0.17,0.11,0.08],[0.23,0.13,0.08]][bh_rnd(s,3)];
function bh_infill(s) = [[0.43,0.41,0.36],[0.41,0.39,0.33],[0.44,0.40,0.32]][bh_rnd(s,3)];
function bh_wood(s) = [[0.27,0.19,0.12],[0.22,0.16,0.10],[0.31,0.23,0.15],[0.19,0.15,0.11]][bh_rnd(s,4)];
function bh_stone(s) = [[0.36,0.35,0.32],[0.41,0.39,0.35],[0.31,0.31,0.29],[0.38,0.34,0.29]][bh_rnd(s,4)];
function bh_brick(s) = [[0.40,0.18,0.12],[0.35,0.17,0.12],[0.44,0.23,0.15]][bh_rnd(s,3)];
function bh_shutter(s) = [[0.16,0.26,0.19],[0.25,0.16,0.10],[0.17,0.21,0.27],[0.30,0.27,0.20]][bh_rnd(s,4)];
function bh_brizolit(s) = [[0.40,0.38,0.34],[0.36,0.35,0.32],[0.42,0.37,0.30]][bh_rnd(s,3)];
function bh_eternit(s) = [[0.29,0.30,0.29],[0.25,0.26,0.26],[0.33,0.32,0.30]][bh_rnd(s,3)];
function bh_frame() = [0.29,0.26,0.21];
function bh_dark() = [0.07,0.07,0.06];
function bh_iron() = [0.17,0.16,0.15];

module bh_box(s) cube(s,center=true);
module bh_slab(L=4,D=4,t=0.2) translate([0,0,t/2]) cube([L,D,t],center=true);
// Beam lying in the x-z plane between two points (timber frames, braces, rafters).
module bh_beam2(a,b,w=0.16,d=0.12)
    translate([(a[0]+b[0])/2,0,(a[1]+b[1])/2]) rotate([0,-atan2(b[1]-a[1],b[0]-a[0]),0])
        bh_box([norm([b[0]-a[0],b[1]-a[1]])+w*0.6,d,w]);

// Thin slabs with explicit outward winding. Points are listed counter-clockwise
// as seen from outside/above; the slab extends th straight down.
module bh_prism4(p0,p1,p2,p3,th=0.1)
    polyhedron(points=[p0,p1,p2,p3,p0-[0,0,th],p1-[0,0,th],p2-[0,0,th],p3-[0,0,th]],
               faces=[[3,2,1,0],[4,5,6,7],[0,1,5,4],[1,2,6,5],[2,3,7,6],[3,0,4,7]]);
module bh_prism3(a,b,c,th=0.1)
    polyhedron(points=[a,b,c,a-[0,0,th],b-[0,0,th],c-[0,0,th]],
               faces=[[2,1,0],[3,4,5],[0,1,4,3],[1,2,5,4],[2,0,3,5]]);

// ================= Parts =================
// Window centred on its opening, wall face at y=0. The plaster surround
// (šambrána) and sill sit on the facade; the glazing is gone or boarded.
module bh_part_window(seed=0,w=1.1,h=1.4,sh=false,tc=[0.48,0.46,0.41])
{
    v=bh_rnd(seed,7);
    color(tc)
    {
        for(sx=[-1,1]) translate([sx*(w/2+0.08),-0.03,0]) bh_box([0.16,0.06,h+0.30]);
        translate([0,-0.03,h/2+0.11]) bh_box([w+0.32,0.06,0.22]);
        translate([0,-0.09,-h/2-0.06]) bh_box([w+0.40,0.22,0.10]);
        if(v%3==0) translate([0,-0.06,h/2+0.30]) bh_box([w+0.52,0.12,0.10]);
    }
    color(bh_frame()) translate([0,0.10,0])
    {
        for(sx=[-1,1]) translate([sx*(w/2-0.04),0,0]) bh_box([0.07,0.07,h]);
        for(sz=[-1,1]) translate([0,0,sz*(h/2-0.04)]) bh_box([w,0.07,0.07]);
        if(v!=6) { translate([0,0,-h*0.12]) bh_box([0.06,0.06,h*0.76]); translate([0,0,h*0.26]) bh_box([w,0.06,0.06]); }
    }
    if(v==1) color(bh_wood(seed)) for(i=[0:2])
        translate([0,-0.07,(i-1)*h*0.31]) rotate([0,bh_rf(seed+i,-11,11),0]) bh_box([w+0.22,0.04,0.17]);
    if(v==3) color(bh_frame()) translate([-w/2+0.02,0.06,0]) rotate([0,0,-38]) translate([w*0.24,0,0]) bh_box([w*0.48,0.05,h*0.92]);
    if(sh) color(bh_shutter(seed)) for(sx=[-1,1]) if(!(v==4 && sx>0))
        translate([sx*(w/2+0.43),-0.07,0]) bh_box([w*0.5,0.05,h]);
    if(sh && v==4) color(bh_shutter(seed)) translate([w/2+0.40,-0.22,-h*0.25]) rotate([0,24,12]) bh_box([w*0.5,0.05,h]);
}

// Wall strip along x, centred on y=0, with rectangular voids. ops=[[xc,zb,w,h],...]
// sorted by xc. The decomposition is exact: piers, then sill and head blocks.
module bh_part_wall(L=10,H=3,ops=[],t=0.4,c=[0.45,0.40,0.30])
{
    n=len(ops);
    e=concat([-L/2],[for(o=ops) each [o[0]-o[2]/2,o[0]+o[2]/2]],[L/2]);
    color(c)
    {
        for(i=[0:n]) if(e[2*i+1]-e[2*i]>0.01)
            translate([(e[2*i]+e[2*i+1])/2,0,H/2]) bh_box([e[2*i+1]-e[2*i],t,H]);
        for(o=ops)
        {
            if(o[1]>0.01) translate([o[0],0,o[1]/2]) bh_box([o[2],t,o[1]]);
            if(H-o[1]-o[3]>0.01) translate([o[0],0,(o[1]+o[3]+H)/2]) bh_box([o[2],t,H-o[1]-o[3]]);
        }
    }
}

// Column centres for a facade: odd counts keep the door on the axis.
function bh_cols(L,door) = let(n=max(1,floor((L-0.6)/2.45))) door && n%2==0 ? (L/(n+1)>=2.0 ? n+1 : max(1,n-1)) : n;

// Plastered facade with plinth, cornices, surrounds and weathering. Wall centred
// on y=0, outer face at -t/2. door: walk-in opening on the axis of the ground floor.
// arch: arched stone portal. shop: wide ground-floor shop windows.
module bh_part_facade(seed=0,L=10,floors=1,fh=3.1,t=0.42,c=[],door=true,dw=1.6,dh=2.45,win=[1.1,1.4],
                      sh=false,plinth=0.65,arch=false,shop=false,cols=0,pilaster=true,sill=0.95)
{
    cc=len(c)>0 ? c : bh_plaster(seed);
    tc=bh_trim(seed+3);
    n=cols>0 ? cols : bh_cols(L,door);
    step=L/n;
    mid=floor(n/2);
    for(f=[0:floors-1])
    {
        z0=f*fh;
        ops=[for(i=[0:n-1]) let(x=-L/2+(i+0.5)*step)
            f==0 && door && i==mid ? [x,0,dw,dh] :
            f==0 && shop ? [x,0.55,min(step-0.7,2.2),2.05] :
            [x,f==0 ? sill : 0.85,win[0],win[1]]];
        translate([0,0,z0]) bh_part_wall(L,fh,ops,t,cc);
        for(o=ops) if(!(f==0 && door && abs(o[0]-(-L/2+(mid+0.5)*step))<0.01))
            translate([o[0],-t/2,z0+o[1]+o[3]/2]) bh_part_window(seed+f*53+round(o[0]*7),o[2],o[3],sh,tc);
        // Solid plaster bands: exposed brick where the render has fallen away.
        for(i=[0:n-1]) if(bh_rnd(seed+f*17+i*29,4)==0)
            color(bh_brick(seed+i)) translate([-L/2+(i+bh_rf(seed+i*3,0.2,0.8))*step,-t/2-0.012,z0+fh-0.42])
                bh_box([bh_rf(seed+i*5,0.6,1.3),0.02,bh_rf(seed+i*7,0.25,0.45)]);
        if(f>0) color(tc) translate([0,-t/2-0.05,z0+0.04]) bh_box([L+0.04,0.12,0.16]);
    }
    // Ground-floor door: frame, threshold and either a stone arch or a lintel.
    dx=-L/2+(mid+0.5)*step;
    if(door)
    {
        color(bh_frame()) for(sx=[-1,1]) translate([dx+sx*(dw/2-0.04),0.02,dh/2]) bh_box([0.08,t*0.5,dh]);
        if(arch)
        {
            color(cc) translate([dx,0,dh-dw/2]) difference()
            {
                translate([0,0,dw/4+0.01]) bh_box([dw,t,dw/2]);
                rotate([90,0,0]) cylinder(h=t+0.2,r=dw/2,center=true,$fn=20);
                translate([0,0,-dw/2]) bh_box([dw+1,t+1,dw]);
            }
            color(bh_stone(seed+5)) translate([dx,-t/2-0.04,dh-dw/2]) difference()
            {
                rotate([90,0,0]) cylinder(h=0.1,r=dw/2+0.24,center=true,$fn=20);
                rotate([90,0,0]) cylinder(h=0.3,r=dw/2,center=true,$fn=20);
                translate([0,0,-dw/2-0.3]) bh_box([dw+1,1,dw+0.6]);
            }
            color(bh_stone(seed+5)) for(sx=[-1,1]) translate([dx+sx*(dw/2+0.12),-t/2-0.04,(dh-dw/2)/2]) bh_box([0.24,0.1,dh-dw/2]);
        }
        else color(tc) translate([dx,-t/2-0.04,dh+0.12]) bh_box([dw+0.5,0.1,0.24]);
        color(bh_stone(seed)) translate([dx,-t/2-0.18,0.04]) bh_box([dw+0.4,0.36,0.08]);
    }
    // Plinth (sokl), corner pilasters, main cornice and soot streaks.
    color(bh_stone(seed+1))
        for(s=door ? [[-L/2,dx-dw/2-0.05],[dx+dw/2+0.05,L/2]] : [[-L/2,L/2]])
            if(s[1]-s[0]>0.05) translate([(s[0]+s[1])/2,-t/2-0.03,plinth/2]) bh_box([s[1]-s[0],0.07,plinth]);
    if(pilaster) color(tc) for(sx=[-1,1]) translate([sx*(L/2-0.22),-t/2-0.03,(floors*fh+plinth)/2]) bh_box([0.44,0.06,floors*fh-plinth]);
    color(tc)
    {
        translate([0,-t/2-0.10,floors*fh-0.12]) bh_box([L+0.10,0.22,0.24]);
        translate([0,-t/2-0.18,floors*fh-0.02]) bh_box([L+0.20,0.18,0.10]);
    }
    for(i=[1:max(1,n-1)]) if(n>1 && bh_rnd(seed+i*13,3)==0)
        color([0.20,0.20,0.18]) translate([-L/2+i*step,-t/2-0.014,floors*fh-1.0]) bh_box([0.22,0.02,1.5]);
}

// Gable wall standing on the eave line (z=0), width W along x, front=-y.
// style: "plain" | "baroque" (folk baroque scrolls rising above the roof) |
// "timber" (Egerland half-timbering) | "boards" (barn cladding). cut>0 truncates
// the top for a half-hipped roof; win: number of attic openings.
function bh_gable_outline(W,r,style,cut) =
    style=="baroque" ?
        let(R=[[1.00,0.00],[1.04,0.00],[1.04,0.10],[0.93,0.12],[0.86,0.22],[0.80,0.31],[0.66,0.37],[0.59,0.47],
               [0.57,0.57],[0.48,0.63],[0.36,0.71],[0.31,0.76],[0.31,0.87],[0.37,0.89],[0.19,0.99],[0.0,1.07]])
        concat([for(p=R) [p[0]*W/2,p[1]*r]],[for(i=[len(R)-2:-1:0]) [-R[i][0]*W/2,R[i][1]*r]]) :
    cut>0.01 ? [[W/2,0],[W/2*cut,r*(1-cut)],[-W/2*cut,r*(1-cut)],[-W/2,0]] :
    [[W/2,0],[0,r],[-W/2,0]];

module bh_part_gable(seed=0,W=8,rise=4,t=0.42,c=[],style="plain",cut=0,win=1,wh=[0.75,0.95],door=[0,0])
{
    cc=len(c)>0 ? c : bh_plaster(seed);
    tc=bh_trim(seed+3);
    top=style=="baroque" ? rise*1.07 : rise*(1-cut);
    wz=style=="baroque" ? rise*0.22 : min(rise*0.3,top-wh[1]-0.5);
    wx=win>=2 ? W*(style=="baroque" ? 0.22 : 0.18) : 0;
    ws=win>=2 ? [-wx,wx] : win==1 ? [0] : [];
    color(style=="timber" ? bh_infill(seed) : style=="boards" ? bh_wood(seed) : cc) difference()
    {
        translate([0,t/2,0]) rotate([90,0,0]) linear_extrude(height=t) polygon(bh_gable_outline(W,rise,style,cut));
        for(x=ws) translate([x,0,wz+wh[1]/2]) bh_box([wh[0],t+0.3,wh[1]]);
        if(style=="baroque") translate([0,-t/2,rise*0.62]) bh_box([0.7,0.24,1.1]);
        if(door[0]>0) translate([0,0,door[1]/2-0.01]) bh_box([door[0],t+0.3,door[1]]);
    }
    for(x=ws) translate([x,-t/2,wz+wh[1]/2]) bh_part_window(seed+round(x*13),wh[0],wh[1],false,tc);
    if(style=="baroque") color(tc)
    {
        translate([0,-t/2-0.05,0.08]) bh_box([W+0.3,0.14,0.16]);
        for(sx=[-1,1]) translate([sx*W*0.30,-t/2-0.03,rise*0.30]) bh_box([0.30,0.06,rise*0.30]);
        translate([0,-t/2-0.05,rise*0.86]) bh_box([W*0.34,0.10,0.12]);
        translate([0,-t/2-0.05,rise*0.62+0.62]) rotate([90,0,0]) cylinder(h=0.1,r=0.42,center=true,$fn=12);
    }
    if(style=="timber") color(bh_beam(seed))
    {
        hw=W/2;
        translate([0,-t/2-0.03,0.08]) bh_box([W,0.08,0.16]);
        for(k=[-floor(hw/1.15):floor(hw/1.15)]) let(x=k*1.15,zt=rise*(1-abs(x)/hw))
            if(zt>0.5 && !(win>=1 && abs(x)<wh[0]/2+0.1 && len(ws)==1)) translate([x,-t/2-0.03,zt/2]) bh_box([0.15,0.08,zt]);
        for(z=[1.0,2.05,3.1]) let(hx=hw*(1-z/rise)) if(hx>0.6)
            translate([0,-t/2-0.03,z]) bh_box([2*hx,0.08,0.14]);
        for(sx=[-1,1]) translate([0,-t/2-0.03,0]) bh_beam2([sx*hw*0.98,0.1],[sx*hw*0.55,min(1.0,rise*0.45)],0.14,0.08);
        for(sx=[-1,1]) translate([0,-t/2-0.035,0]) bh_beam2([sx*hw,0],[0,rise],0.18,0.08);
    }
    if(style=="boards") color(bh_wood(seed+7))
        for(x=[-W/2+0.35:0.62:W/2]) let(zt=rise*(1-abs(x)/(W/2)),zb=door[0]>0 && abs(x)<door[0]/2+0.06 ? door[1] : 0)
            if(zt-zb>0.3) translate([x,-t/2-0.02,(zt+zb)/2]) bh_box([0.08,0.04,zt-zb]);
}

// Tile roof with ridge along x; eave line of the walls at z=0, y=±D/2.
// hip: fraction of the height that is hipped (0 gable, ~0.3 Bohemian half-hip,
// 1 full hip). Tile courses are separate overlapping strips; dmg opens holes
// over exposed rafters. metal=true gives corrugated eternit sheets.
function bh_rhx(t,th,hx0,E) = t<=th ? hx0 : hx0-(t-th)*E;
module bh_part_roof_slope(seed=0,L=10,D=8,rise=4,ovx=0.35,ovy=0.45,hip=0,dmg=1,metal=false)
{
    E=D/2+ovy;
    ze=-ovy*rise/(D/2);
    zr=rise;
    hx0=L/2+(hip>0 ? ovy : ovx);
    th=1-hip;
    rx=max(0.05,hx0-hip*E);
    sl=norm([E,zr-ze]);
    ang=atan2(zr-ze,E);
    nr=max(3,ceil(sl/(metal ? 1.3 : 0.62)));
    holes=[for(i=[0:max(1,dmg)-1]) let(k0=bh_rnd(seed+i*31,max(1,nr-3)))
        [bh_rf(seed+i*7,-hx0*0.55,hx0*0.55),bh_rf(seed+i*11,1.1,metal ? 4.5 : 2.8),k0,k0+1+bh_rnd(seed+i*13,3)]];
    for(k=[0:nr-1])
    {
        t0=k/nr; t1=min(1,(k+1)/nr+0.035);
        a0=bh_rhx(t0,th,hx0,E); a1=bh_rhx(t1,th,hx0,E);
        y0=-E*(1-t0); y1=-E*(1-t1); z0=ze+(zr-ze)*t0; z1=ze+(zr-ze)*t1;
        cuts=[for(hl=holes) if(dmg>0 && k>=hl[2] && k<=hl[3]) hl];
        segs=len(cuts)==0 ? [[-1e3,1e3]] : [[-1e3,cuts[0][0]-cuts[0][1]/2],[cuts[0][0]+cuts[0][1]/2,1e3]];
        tint=metal ? bh_eternit(seed+k) : bh_tile(seed+bh_rnd(seed+k,2));
        color(tint) for(g=segs)
        {
            l0=max(-a0,g[0]); r0=min(a0,g[1]); l1=max(-a1,g[0]); r1=min(a1,g[1]);
            if(r0-l0>0.08 && r1-l1>0.02)
                bh_prism4([l0,y0,z0-0.035],[r0,y0,z0-0.035],[r1,y1,z1],[l1,y1,z1],metal ? 0.06 : 0.09);
        }
        if(metal && k%2==0) color(bh_eternit(seed)) for(x=[-a0+0.4:1.2:a0-0.3])
            translate([x,(y0+y1)/2,(z0+z1)/2+0.02]) rotate([ang,0,0]) bh_box([0.08,sl/nr,0.05]);
    }
    // Moss and lichen patches lie on the tiles rather than colouring whole courses.
    if(!metal) for(i=[0:1]) let(xc=bh_rf(seed+i*41,-hx0*0.7,hx0*0.7),w=bh_rf(seed+i*43,0.8,2.2),ta=bh_rf(seed+i*47,0.05,0.55),tb=ta+bh_rf(seed+i*53,0.08,0.22))
        if(bh_rnd(seed+i*59,3)!=0 && abs(xc)+w/2<bh_rhx(tb,th,hx0,E)) color(bh_moss(seed+i))
            bh_prism4([xc-w/2,-E*(1-ta),ze+(zr-ze)*ta+0.012],[xc+w/2,-E*(1-ta),ze+(zr-ze)*ta+0.012],
                      [xc+w*0.35,-E*(1-tb),ze+(zr-ze)*tb+0.012],[xc-w*0.4,-E*(1-tb),ze+(zr-ze)*tb+0.012],0.03);
    // Rafters show through the holes.
    if(dmg>0) color([0.20,0.16,0.11]) for(hl=holes) for(x=[hl[0]-hl[1]/2+0.3:0.75:hl[0]+hl[1]/2])
        if(abs(x)<rx+E*0.5) translate([x,-E*0.5,(ze+zr)/2-0.22]) rotate([ang,0,0]) bh_box([0.12,sl,0.16]);
    // Hip / half-hip end at +x; the 180-degree copy supplies the -x end.
    if(hip>0) color(metal ? bh_eternit(seed) : bh_tile(seed+1))
        bh_prism3([hx0,-E*hip,ze+(zr-ze)*th],[hx0,E*hip,ze+(zr-ze)*th],[rx,0,zr],metal ? 0.06 : 0.09);
    // Barge board along the gable verge.
    if(hip<=0) color(bh_wood(seed)) translate([hx0-0.04,-E*0.5,(ze+zr)/2+0.02]) rotate([ang,0,0]) bh_box([0.06,sl+0.1,0.24]);
}

module bh_part_roof(seed=0,L=10,D=8,rise=4,ovx=0.35,ovy=0.45,hip=0,dmg=1,metal=false)
{
    bh_part_roof_slope(seed,L,D,rise,ovx,ovy,hip,dmg,metal);
    rotate([0,0,180]) bh_part_roof_slope(seed+77,L,D,rise,ovx,ovy,hip,max(0,dmg-1),metal);
    rx=max(0.05,L/2+(hip>0 ? ovy : ovx)-hip*(D/2+ovy));
    color(metal ? bh_eternit(seed+2) : bh_tile(seed+2)) translate([0,0,rise+0.03]) rotate([0,90,0]) cylinder(h=2*rx+0.1,r=0.14,center=true,$fn=8);
}

module bh_part_chimney(seed=0,h=1.8,w=0.62)
{
    color(bh_brick(seed)) bh_slab(w,w,h);
    color(bh_stone(seed)) translate([0,0,h]) bh_slab(w+0.14,w+0.14,0.12);
    color(bh_dark()) translate([0,0,h+0.05]) bh_slab(w*0.5,w*0.5,0.09);
}

// Dormer (vikýř) whose front stands at y=0, sitting on a slope of angle ang.
module bh_part_dormer(seed=0,w=1.6,h=1.5,ang=40,c=[0.4,0.4,0.4])
{
    color(c) difference()
    {
        translate([0,1.1,h/2]) bh_box([w,2.2,h]);
        translate([0,-0.1,h*0.5]) bh_box([w*0.55,0.6,h*0.6]);
    }
    translate([0,0,h*0.5]) bh_part_window(seed,w*0.55,h*0.6,false,bh_trim(seed));
    color(bh_tile(seed)) for(sx=[-1,1]) translate([sx*w*0.27,1.0,h+0.32]) rotate([0,sx*35,0]) bh_box([w*0.66,2.5,0.08]);
}

// Rubble, fallen tiles and weeds hugging the plinth; keeps the entrance clear.
module bh_part_decay(seed=0,L=10,D=8)
{
    for(i=[0:7])
    {
        sx=bh_rnd(seed+i*23,2)==0 ? -1 : 1;
        color(i%3==0 ? bh_tile(seed+i) : bh_stone(seed+i))
            translate([sx*bh_rf(seed+i*11,L*0.30,L*0.46),bh_rf(seed+i*19,-D*0.40,D*0.40),0.08])
                rotate([6,bh_rf(seed+i,-15,15),bh_rnd(seed+i*7,180)]) bh_box([bh_rf(seed+i,0.2,0.55),0.24,0.12]);
    }
    for(i=[0:5]) color(bh_moss(seed+i))
        translate([(i%2==0 ? -1 : 1)*(L/2+0.12),bh_rf(seed+i*17,-D*0.4,D*0.4),0])
            for(a=[-35,0,35]) rotate([a,0,i*40]) cylinder(h=bh_rf(seed+i,0.5,1.1),r1=0.08,r2=0.01,$fn=5);
}

// Ivy climbing a wall face at y=0 (front=-y), base z=0.
module bh_part_ivy(seed=0,h=3,w=1.6)
{
    for(i=[0:5]) color(bh_moss(seed+i))
        translate([bh_rf(seed+i*3,-w/2,w/2),-0.04,bh_rf(seed+i*5,0.2,h*0.9)])
            bh_box([bh_rf(seed+i*7,0.4,0.9),0.06,bh_rf(seed+i*11,0.5,1.4)]);
}

// Egerland half-timbering laid over an upper-floor facade of n window bays.
module bh_part_frame(seed=0,L=10,H=2.8,n=4,win=[0.95,1.15],zb=0.85,t=0.42)
{
    step=L/n;
    color(bh_beam(seed)) translate([0,-t/2-0.035,0])
    {
        translate([0,0,0.09]) bh_box([L,0.08,0.18]);
        translate([0,0,H-0.09]) bh_box([L,0.08,0.18]);
        translate([0,0,zb-0.07]) bh_box([L,0.07,0.12]);
        translate([0,0,zb+win[1]+0.07]) bh_box([L,0.07,0.12]);
        for(i=[0:n]) translate([-L/2+i*step+(i==0 ? 0.08 : i==n ? -0.08 : 0),0,H/2]) bh_box([0.16,0.08,H]);
        for(i=[0:n-1]) for(sx=[-1,1]) translate([-L/2+(i+0.5)*step+sx*(win[0]/2+0.08),0,H/2]) bh_box([0.13,0.08,H]);
        for(i=[0:n]) for(sx=[-1,1]) if((i>0 || sx>0) && (i<n || sx<0))
            bh_beam2([-L/2+i*step,0.15],[-L/2+i*step+sx*(step/2-win[0]/2-0.12),zb-0.12],0.12,0.07);
    }
}

// Vertical board cladding between z0 and z1, outer face near y=0; gate void |x|<gw/2 below gh.
module bh_part_boards(seed=0,L=10,z0=0.9,z1=4.5,gw=0,gh=0)
{
    color(bh_wood(seed)) for(x=[-L/2+0.17:0.34:L/2-0.1]) if(bh_rnd(seed+round(x*31),13)!=0)
    {
        if(abs(x)<gw/2+0.05) { if(z1-gh>0.05) translate([x,0,(gh+z1)/2]) bh_box([0.30,0.05,z1-gh]); }
        else translate([x,0,(z0+z1)/2]) bh_box([0.30,0.05,z1-z0]);
    }
    color(bh_wood(seed+1)) for(z=[z0+0.3,(z0+z1)/2,z1-0.2])
        for(r=gw>0 && z<gh ? [[-L/2,-gw/2],[gw/2,L/2]] : [[-L/2,L/2]])
            translate([(r[0]+r[1])/2,0.06,z]) bh_box([r[1]-r[0],0.06,0.14]);
}

// Onion dome (cibulová báně) with lantern and cross, base at z=0.
module bh_part_onion(seed=0,r=1.3,h=4.2)
{
    P=[[0,0],[1.0,0],[1.0,0.07],[0.84,0.11],[0.97,0.21],[1.06,0.29],[0.96,0.39],[0.58,0.49],[0.24,0.55],[0.20,0.59],
       [0.52,0.65],[0.52,0.73],[0.20,0.77],[0.30,0.83],[0.11,0.92],[0.04,1.0],[0,1.0]];
    color(bh_rnd(seed,2)==0 ? [0.20,0.29,0.25] : [0.17,0.18,0.19]) rotate_extrude($fn=12) polygon([for(p=P) [p[0]*r,p[1]*h]]);
    color([0.36,0.31,0.17]) translate([0,0,h]) { translate([0,0,0.55]) bh_box([0.07,0.07,1.1]); translate([0,0,0.75]) bh_box([0.55,0.07,0.07]); }
}

module bh_part_crenel(len=10,wt=1.4)
    for(x=[-len/2+0.5:1.6:len/2-0.4]) translate([x,0,0]) bh_slab(0.8,wt,0.9);

// Tall church wall with arched window heads; ops sorted by x.
module bh_part_church_wall(seed=0,L=12,H=9.5,t=0.7,c=[0.46,0.42,0.32],ops=[])
{
    tc=bh_trim(seed);
    bh_part_wall(L,H,ops,t,c);
    for(o=ops) if(o[1]>0)
    {
        color(tc) translate([o[0],-t/2-0.04,o[1]+o[3]]) rotate([90,0,0]) difference()
        {
            cylinder(h=0.08,r=o[2]/2+0.22,center=true,$fn=16);
            cylinder(h=0.3,r=o[2]/2,center=true,$fn=16);
            translate([0,-1,0]) bh_box([3,2,0.5]);
        }
        color(bh_dark()) translate([o[0],-t/2-0.02,o[1]+o[3]]) rotate([90,0,0]) difference()
        {
            cylinder(h=0.03,r=o[2]/2,center=true,$fn=16);
            translate([0,-1,0]) bh_box([3,2,0.5]);
        }
        color(bh_frame()) translate([o[0],0,o[1]+o[3]/2]) bh_box([0.08,0.1,o[3]]);
        color(tc) translate([o[0],-t/2-0.08,o[1]-0.06]) bh_box([o[2]+0.4,0.2,0.12]);
    }
    color(tc) translate([0,-t/2-0.08,H-0.15]) bh_box([L+0.2,0.2,0.3]);
    color(bh_stone(seed+1)) translate([0,-t/2-0.03,0.4]) bh_box([L,0.07,0.8]);
}

// ================= Buildings =================
// Single-storey farmhouse, ridge along x. gable=+1 puts the decorated street
// gable on +x (-1 on -x). style: 0 folk baroque, 1 plastered half-hip,
// 2 Egerland timber gable, 3 plain gable; -1 picks by seed.
module bh_bldg_farmhouse(seed=0,L=12,D=8,gable=1,style=-1)
{
    st=style>=0 ? style : bh_rnd(seed,4);
    h=3.05; t=0.42; c=bh_plaster(seed);
    rise=D*0.6;
    hip=st==1 ? 0.3 : 0;
    gst=st==0 ? "baroque" : st==2 ? "timber" : "plain";
    color(bh_stone(seed+2)) bh_slab(L,D,0.12);
    translate([0,-D/2+t/2,0]) bh_part_facade(seed,L,1,h,t,c,true,1.5,2.35,[1.0,1.25],true,0.6);
    translate([0,D/2-t/2,0]) rotate([0,0,180]) bh_part_facade(seed+31,L,1,h,t,c,false,1.5,2.35,[1.0,1.25],true,0.6);
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90])
    {
        bh_part_facade(seed+41+sx,D-2*t,1,h,t,c,false,1.5,2.35,[0.95,1.2],true,0.6,cols=sx==gable ? 2 : 1,pilaster=false);
        translate([0,0,h]) bh_part_gable(seed+sx*7,D,rise,t,c,sx==gable ? gst : st==2 ? "timber" : "plain",hip,sx==gable ? 2 : 1);
    }
    color([0.25,0.23,0.20]) translate([0,0,h-0.12]) bh_slab(L-2*t,D-2*t,0.12);
    translate([0,0,h]) bh_part_roof(seed,L,D,rise,gst=="baroque" ? 0.02 : 0.35,0.45,hip,1+bh_rnd(seed+9,2));
    translate([-gable*L*0.18,0.35,h+rise*0.78]) bh_part_chimney(seed,rise*0.38+1.0);
    bh_part_decay(seed,L,D);
    if(bh_rnd(seed+4,3)==0) translate([-gable*(L/2-1.3),-D/2-0.02,0]) bh_part_ivy(seed,h*0.9,2.2);
}

// Two-storey Egerland (Chebsko) house: masonry ground floor, half-timbered
// upper floor and gables, steep tile roof.
module bh_bldg_egerland(seed=0,L=13,D=9,gable=1)
{
    h1=3.0; h2=2.75; t=0.42; rise=D*0.62;
    c=bh_plaster(seed+5); inf=bh_infill(seed);
    nf=bh_cols(L,false); ns=max(1,bh_cols(D-2*t,false));
    color(bh_stone(seed+2)) bh_slab(L,D,0.12);
    color([0.25,0.23,0.20]) translate([0,0,h1]) bh_slab(L-2*t,D-2*t,0.14);
    translate([0,-D/2+t/2,0]) bh_part_facade(seed,L,1,h1,t,c,true,1.5,2.35,[1.0,1.25],true,0.6);
    translate([0,D/2-t/2,0]) rotate([0,0,180]) bh_part_facade(seed+31,L,1,h1,t,c,false,1.5,2.35,[1.0,1.25],true,0.6);
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90])
        bh_part_facade(seed+41+sx,D-2*t,1,h1,t,c,false,1.5,2.35,[0.95,1.2],true,0.6,pilaster=false);
    for(r=[[0,-D/2+t/2,0,L,nf],[0,D/2-t/2,180,L,nf],[L/2-t/2,0,90,D-2*t,ns],[-L/2+t/2,0,-90,D-2*t,ns]])
        translate([r[0],r[1],h1]) rotate([0,0,r[2]])
        {
            bh_part_facade(seed+r[2]+7,r[3],1,h2,t,inf,false,1,2,[0.95,1.15],false,0,cols=r[4],pilaster=false,sill=0.85);
            bh_part_frame(seed+r[2],r[3],h2,r[4],[0.95,1.15],0.85,t);
        }
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,h1+h2]) rotate([0,0,sx*90]) bh_part_gable(seed+sx*5,D,rise,t,[],"timber",0,sx==gable ? 2 : 1);
    translate([0,0,h1+h2]) bh_part_roof(seed,L,D,rise,0.4,0.5,0,1+bh_rnd(seed+3,2));
    translate([gable*L*0.2,-0.3,h1+h2+rise*0.76]) bh_part_chimney(seed,rise*0.4+1.0);
    bh_part_decay(seed,L,D);
}

// Plank-clad barn (stodola) on a stone plinth, gated through both long sides.
module bh_bldg_barn(seed=0,L=16,D=9)
{
    h=4.6; ph=0.9; t=0.5; rise=D*0.62; gw=min(3.8,L*0.3); gh=3.9;
    color(bh_stone(seed)) bh_slab(L,D,0.12);
    for(sy=[-1,1]) translate([0,sy*(D/2-t/2),0]) bh_part_wall(L,ph,[[0,0,gw,ph]],t,bh_stone(seed+sy));
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,90]) bh_part_wall(D-2*t,ph,[],t,bh_stone(seed+sx+3));
    for(sy=[-1,1]) translate([0,sy*(D/2-0.15),0]) rotate([0,0,sy<0 ? 0 : 180]) bh_part_boards(seed+sy*13,L,ph,h,gw,gh);
    for(sx=[-1,1]) translate([sx*(L/2-0.15),0,0]) rotate([0,0,sx*90]) bh_part_boards(seed+sx*29,D,ph,h,0,0);
    for(sx=[-1,1]) translate([sx*(L/2-0.15),0,h]) rotate([0,0,sx*90]) bh_part_gable(seed+sx,D,rise,0.2,[],"boards",0,1,[1.3,1.5]);
    color(bh_wood(seed+3))
    {
        translate([-gw/2,-D/2-0.1,gh/2+0.1]) rotate([0,0,-100]) translate([gw/4,0,0]) bh_box([gw/2,0.08,gh-0.2]);
        translate([gw/2+gw/4+0.6,-D/2-0.7,gh/2-0.15]) rotate([0,-9,-18]) bh_box([gw/2,0.08,gh-0.3]);
        for(sx=[-1,1]) translate([sx*gw/2,D/2+0.1,gh/2+0.1]) rotate([0,0,-sx*70]) translate([-sx*gw/4,0,0]) bh_box([gw/2,0.08,gh-0.2]);
    }
    color(bh_wood(seed+5)) for(x=[-L/2+2:3:L/2-1]) translate([x,0,h-0.15]) bh_box([0.2,D-0.3,0.22]);
    translate([0,0,h]) bh_part_roof(seed,L,D,rise,0.45,0.5,0,2);
    bh_part_decay(seed,L,D);
}

// Low side wing: stable / výměnek, small high windows, door on -y.
module bh_bldg_stable(seed=0,L=11,D=7)
{
    h=2.9; t=0.45; c=bh_rnd(seed,2)==0 ? bh_stone(seed) : bh_plaster(seed+9); rise=D*0.55;
    color(bh_stone(seed+2)) bh_slab(L,D,0.12);
    translate([0,-D/2+t/2,0]) bh_part_facade(seed,L,1,h,t,c,true,1.6,2.3,[0.8,0.65],false,0.5,pilaster=false,sill=1.75);
    translate([0,D/2-t/2,0]) rotate([0,0,180]) bh_part_facade(seed+3,L,1,h,t,c,false,1,2,[0.8,0.65],false,0.5,pilaster=false,sill=1.75);
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90])
    {
        bh_part_wall(D-2*t,h,[],t,c);
        translate([0,0,h]) bh_part_gable(seed+sx,D,rise,t,c,"plain",0,1,[0.6,0.7]);
    }
    translate([0,0,h]) bh_part_roof(seed,L,D,rise,0.35,0.4,0,1+bh_rnd(seed,2));
    bh_part_decay(seed,L,D);
}

// Town burgher house for a continuous street front. Party walls on ±x are blank.
// front: 1 baroque gable to the street, 0 eaves + dormers, -1 by seed.
module bh_bldg_burgher(seed=0,L=8,D=12,floors=2,front=-1,shop=false)
{
    fh=3.3; h=floors*fh; t=0.5;
    g=front>=0 ? front==1 : bh_rnd(seed+5,3)==0;
    c=bh_plaster(seed);
    color(bh_stone(seed)) bh_slab(L,D,0.12);
    color([0.26,0.24,0.20]) for(f=[1:floors]) translate([0,0,f*fh-0.14]) bh_slab(L-2*t,D-2*t,0.14);
    translate([0,-D/2+t/2,0]) bh_part_facade(seed,L,floors,fh,t,c,true,1.8,2.8,[1.1,1.55],false,0.7,true,shop);
    translate([0,D/2-t/2,0]) rotate([0,0,180]) bh_part_facade(seed+3,L,floors,fh,t,bh_plaster(seed+3),true,1.2,2.3,[1.0,1.4],false,0.5,pilaster=false);
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90])
    {
        bh_part_wall(D-2*t,h,[],t,c);
        color(bh_brick(seed+sx)) translate([bh_rf(seed+sx,-D*0.3,D*0.3),-t/2-0.01,bh_rf(seed+sx*3,1.2,h-1.5)])
            bh_box([bh_rf(seed,1,2.4),0.02,bh_rf(seed+1,0.6,1.4)]);
    }
    if(g)
    {
        rise=L*0.62;
        translate([0,0,h]) rotate([0,0,90]) bh_part_roof(seed,D,L,rise,0.05,0.12,0,1);
        translate([0,-D/2+t/2,h]) bh_part_gable(seed+1,L,rise,t,c,"baroque",0,2);
        translate([0,D/2-t/2,h]) rotate([0,0,180]) bh_part_gable(seed+2,L,rise,t,c,"plain",0,1);
        translate([L*0.22,D*0.15,h+rise*0.5]) bh_part_chimney(seed,rise*0.62+1);
    }
    else
    {
        rise=D*0.36;
        translate([0,0,h]) bh_part_roof(seed,L,D,rise,0.0,0.5,0,1);
        for(sx=[-1,1]) translate([sx*(L/2-t/2),0,h]) rotate([0,0,sx*90]) bh_part_gable(seed+sx,D,rise,t,c,"plain",0,0);
        for(x=L>=9 ? [-L*0.24,L*0.24] : [0]) translate([x,-D/2+1.5,h+rise*1.5/(D/2)-0.25]) bh_part_dormer(seed+round(x),1.5,1.45,0,c);
        for(sx=[-1,1]) translate([sx*(L/2-0.5),D*0.12,h+rise*0.6]) bh_part_chimney(seed+sx,rise*0.5+1);
    }
    if(shop) color(bh_shutter(seed+2)) translate([0,-D/2-0.05,fh-0.45]) bh_box([L*0.7,0.08,0.42]);
    bh_part_decay(seed,L,D);
}

// Town hall: wide burgher front with a clock turret and onion cap on the ridge.
module bh_bldg_townhall(seed=0,L=16,D=12)
{
    bh_bldg_burgher(seed,L,D,2,0,false);
    rise=D*0.36;
    translate([0,0,6.6+rise-0.6])
    {
        color(bh_trim(seed)) bh_slab(2.6,2.6,3.4);
        for(a=[0:90:270]) rotate([0,0,a]) translate([0,-1.31,2.1])
        {
            color([0.44,0.42,0.37]) rotate([90,0,0]) cylinder(h=0.06,r=0.72,center=true,$fn=16);
            color(bh_dark()) { translate([0,-0.05,0.18]) bh_box([0.07,0.03,0.5]); translate([0.16,-0.05,0]) bh_box([0.36,0.03,0.07]); }
        }
        color(bh_trim(seed+1)) translate([0,0,3.4]) bh_slab(2.9,2.9,0.18);
        translate([0,0,3.58]) bh_part_onion(seed,1.25,4.2);
    }
}

// Baroque church: tower with onion dome at -x, nave, apse at +x and the
// south portal on -y at x=0 (inside the nave span).
module bh_bldg_church(seed=0,L=24,D=11)
{
    tw=6.2; t=0.7; nh=9.5; ar=D/2-0.6;
    nx0=-L/2+tw; nx1=L/2-ar; nL=nx1-nx0; ncx=(nx0+nx1)/2;
    c=[[0.47,0.42,0.30],[0.46,0.44,0.38],[0.47,0.38,0.22]][bh_rnd(seed,3)];
    tc=bh_trim(seed);
    wx=[for(k=[0:3]) nx0+nL*(k+0.5)/4];
    south=[for(x=wx) if(abs(x)>2.7) [x-ncx,3.2,1.5,4.4]];
    sops=concat([for(o=south) if(o[0]< -ncx) o],[[-ncx,0,2.2,3.6]],[for(o=south) if(o[0]> -ncx) o]);
    nops=[for(k=[3:-1:0]) [ncx-wx[k],3.2,1.5,4.4]];
    color(bh_stone(seed)) bh_slab(L,D,0.14);
    translate([ncx,-D/2+t/2,0]) bh_part_church_wall(seed,nL,nh,t,c,sops);
    translate([ncx,D/2-t/2,0]) rotate([0,0,180]) bh_part_church_wall(seed+1,nL,nh,t,c,nops);
    color(bh_stone(seed+2))
    {
        for(sx=[-1,1]) translate([sx*1.35,-D/2-0.06,1.9]) bh_box([0.5,0.14,3.8]);
        translate([0,-D/2-0.08,4.0]) bh_box([3.4,0.18,0.4]);
        translate([0,-D/2-0.1,4.2]) rotate([90,0,0]) linear_extrude(height=0.1,center=true) polygon([[-1.8,0],[1.8,0],[0,1.2]]);
        translate([0,-D/2-0.3,0.06]) bh_box([3.0,0.6,0.12]);
    }
    translate([nx1,0,0])
    {
        color(c) difference()
        {
            cylinder(h=nh-0.6,r=ar+t/2,$fn=16);
            translate([0,0,-0.1]) cylinder(h=nh,r=ar-t/2,$fn=16);
            translate([-ar-2,0,nh/2]) bh_box([2*ar+4,2*ar+4,nh+1]);
            for(a=[-45,0,45]) rotate([0,0,a]) translate([ar,0,4.8]) bh_box([2,1.1,3.2]);
        }
        color(bh_tile(seed)) intersection()
        {
            translate([0,0,nh-0.7]) cylinder(h=D*0.48,r1=ar+0.7,r2=0.05,$fn=16);
            translate([ar+2,0,nh+4]) bh_box([2*ar+4,2*ar+6,12]);
        }
    }
    translate([ncx,0,nh]) bh_part_roof(seed,nL,D,D*0.58,0.2,0.55,0,1);
    translate([nx1-t/2,0,nh]) rotate([0,0,90]) bh_part_gable(seed+3,D,D*0.58,t,c,"plain",0,0);
    th=24;
    translate([-L/2+tw/2,0,0])
    {
        color(c) difference()
        {
            bh_slab(tw,tw,th);
            translate([0,0,-0.1]) bh_slab(tw-2*t,tw-2*t,th+0.2);
            translate([tw/2,0,1.6]) bh_box([2*t+0.2,2.2,3.2]);
            for(a=[0:90:270]) rotate([0,0,a]) translate([0,-tw/2,th-3.4]) bh_box([1.3,2*t+0.2,2.6]);
            for(a=[90,270]) rotate([0,0,a]) translate([0,-tw/2,nh+3]) bh_box([0.7,2*t+0.2,1.2]);
        }
        color(bh_wood(seed)) for(a=[0:90:270]) rotate([0,0,a]) for(z=[th-4.4:0.4:th-2.3]) translate([0,-tw/2+t/2,z]) rotate([30,0,0]) bh_box([1.25,0.05,0.3]);
        color(tc) for(z=[nh,th-0.2]) translate([0,0,z]) bh_slab(tw+0.4,tw+0.4,0.3);
        color(tc) for(sx=[-1,1],sy=[-1,1]) translate([sx*(tw/2-0.25),sy*(tw/2-0.25),0]) bh_slab(0.55,0.55,th);
        translate([0,0,th+0.1]) { color(bh_slate(seed)) cylinder(h=1.2,r1=tw*0.62,r2=tw*0.42,$fn=8); translate([0,0,1.2]) bh_part_onion(seed,tw*0.42,7.5); }
    }
    bh_part_decay(seed,L,D);
}

// Village chapel (kaple): gable front with door, apse at +y, bell turret.
module bh_bldg_chapel(seed=0,L=6,D=8)
{
    h=4.2; t=0.5; rise=L*0.62; ar=L/2-0.05;
    c=[[0.47,0.43,0.33],[0.46,0.44,0.40],[0.47,0.39,0.25]][bh_rnd(seed,3)];
    ry=D-L/2;
    color(bh_stone(seed)) bh_slab(L,D,0.12);
    translate([0,-D/2+t/2,0]) bh_part_facade(seed,L,1,h,t,c,true,1.4,2.6,[0.6,1.4],false,0.4,true,cols=1);
    for(sx=[-1,1]) translate([sx*(L/2-t/2),-D/2+ry/2,0]) rotate([0,0,sx*90]) bh_part_wall(ry-t,h,[[0,1.4,0.7,1.6]],t,c);
    translate([0,-D/2+t/2,h]) bh_part_gable(seed,L,rise,t,c,"baroque",0,0);
    translate([0,D/2-ar,0])
    {
        color(c) difference()
        {
            cylinder(h=h,r=ar,$fn=14);
            translate([0,0,-0.1]) cylinder(h=h+0.2,r=ar-t,$fn=14);
            translate([0,-ar-1,h/2]) bh_box([2*ar+2,2*ar+2,h+1]);
        }
        color(bh_tile(seed)) intersection()
        {
            translate([0,0,h-0.1]) cylinder(h=rise,r1=ar+0.4,r2=0.05,$fn=14);
            translate([0,ar+2,h+2]) bh_box([2*ar+4,2*ar+4,8]);
        }
    }
    translate([0,-D/2+ry/2,h]) rotate([0,0,90]) bh_part_roof(seed,ry,L,rise,0.0,0.35,0,0);
    translate([0,-D/2+1.4,h+rise*0.62]) { color(bh_wood(seed)) bh_slab(1.1,1.1,1.7); translate([0,0,1.7]) bh_part_onion(seed+1,0.75,2.4); }
    translate([L/2-0.2,-D/2,0]) bh_part_ivy(seed,3,1.4);
}

// Volunteer fire station (hasičská zbrojnice) with hose-drying tower.
module bh_bldg_firestation(seed=0,L=11,D=8)
{
    h=3.7; t=0.42; c=bh_plaster(seed); rise=D*0.48; gx=L*0.24;
    color(bh_stone(seed)) bh_slab(L,D,0.12);
    translate([0,-D/2+t/2,0]) bh_part_wall(L,h,[[-gx,0,2.8,3.0],[gx,0,2.8,3.0]],t,c);
    color(bh_stone(seed+1)) translate([0,-D/2-0.03,0.3]) bh_box([0.9*(2*gx-2.8),0.07,0.6]);
    color(bh_trim(seed)) translate([0,-D/2-0.05,h-0.15]) bh_box([L+0.1,0.14,0.3]);
    color([0.36,0.10,0.08])
    {
        translate([-gx-0.72,-D/2-0.6,1.5]) rotate([0,0,-70]) bh_box([1.4,0.06,2.9]);
        translate([gx+0.7,-D/2-0.05,1.45]) bh_box([1.38,0.06,2.9]);
        translate([gx-0.3,-D/2-0.9,0.06]) rotate([0,82,20]) bh_box([1.38,0.06,2.6]);
    }
    translate([0,D/2-t/2,0]) rotate([0,0,180]) bh_part_facade(seed+3,L,1,h,t,c,true,1.1,2.2,[1.0,1.2],false,0.5,pilaster=false);
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90])
    {
        bh_part_wall(D-2*t,h,[[0,1.0,1.0,1.2]],t,c);
        translate([0,0,h]) bh_part_gable(seed+sx,D,rise,t,c,"plain",0,1);
    }
    translate([0,0,h]) bh_part_roof(seed,L,D,rise,0.35,0.4,0,1);
    tw=2.6; th=11.5;
    translate([L/2-tw/2-0.1,D/2-tw/2-0.1,0])
    {
        color(c) difference()
        {
            bh_slab(tw,tw,th);
            translate([0,0,0.2]) bh_slab(tw-0.6,tw-0.6,th);
            for(a=[0:90:270]) rotate([0,0,a]) translate([0,-tw/2,th-1.4]) bh_box([1.1,0.8,1.6]);
        }
        color(bh_wood(seed)) for(a=[0:90:270]) rotate([0,0,a]) for(z=[th-2.0:0.35:th-0.8]) translate([0,-tw/2+0.15,z]) rotate([30,0,0]) bh_box([1.05,0.04,0.25]);
        color(bh_tile(seed+1)) translate([0,0,th]) rotate([0,0,45]) cylinder(h=2.2,r1=tw*0.85,r2=0.05,$fn=4);
        color(bh_iron()) translate([0,0,th+2.1]) cylinder(h=0.9,r=0.04,$fn=6);
    }
    color([0.42,0.40,0.36]) translate([-L*0.2,0,h+rise+0.05]) { cylinder(h=0.6,r=0.06,$fn=6); translate([0,0,0.6]) cylinder(h=0.35,r=0.28,$fn=10); }
    bh_part_decay(seed,L,D);
}

// Spa-region villa: two floors, hipped roof, timber-gabled front bay and an
// octagonal corner turret.
module bh_bldg_villa(seed=0,L=12,D=10)
{
    fh=3.3; h=2*fh; t=0.45; c=[[0.47,0.43,0.33],[0.45,0.40,0.30],[0.43,0.41,0.37],[0.46,0.36,0.27]][bh_rnd(seed,4)];
    rise=D*0.42;
    color(bh_stone(seed)) bh_slab(L,D,0.12);
    color([0.26,0.24,0.20]) translate([0,0,fh]) bh_slab(L-2*t,D-2*t,0.14);
    translate([0,-D/2+t/2,0]) bh_part_facade(seed,L,2,fh,t,c,true,1.6,2.6,[1.1,1.6],false,0.8,true);
    translate([0,D/2-t/2,0]) rotate([0,0,180]) bh_part_facade(seed+3,L,2,fh,t,c,false,1,2,[1.1,1.6],false,0.8);
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90]) bh_part_facade(seed+5+sx,D-2*t,2,fh,t,c,false,1,2,[1.0,1.5],false,0.8,pilaster=false);
    translate([0,0,h]) bh_part_roof(seed,L,D,rise,0.5,0.6,1,1,false);
    translate([0,-D/2+t/2,h]) bh_part_gable(seed+9,4.8,2.9,t,c,"timber",0,1,[0.8,1.0]);
    translate([0,-D/2+D*0.22,h]) rotate([0,0,90]) bh_part_roof(seed+4,D*0.44,4.8,2.9,0.3,0.35,0,0);
    color(bh_wood(seed+2))
    {
        translate([0,-D/2-0.6,fh-0.05]) bh_slab(3.4,1.2,0.14);
        for(x=[-1.6:0.2:1.6]) translate([x,-D/2-1.15,fh+0.55]) bh_box([0.05,0.05,1.0]);
        translate([0,-D/2-1.15,fh+1.05]) bh_box([3.4,0.08,0.08]);
        for(sx=[-1,1]) translate([sx*1.5,-D/2-0.35,fh-0.5]) rotate([45,0,0]) bh_box([0.12,0.12,1.0]);
    }
    translate([-L/2+1.5,-D/2+1.5,0])
    {
        color(c) difference()
        {
            cylinder(h=h+2.6,r=1.75,$fn=8);
            for(a=[0:45:315]) if(a!=45) rotate([0,0,a+22.5]) translate([1.7,0,h+1.2]) bh_box([0.6,0.6,1.1]);
            translate([0,0,h-0.1]) cylinder(h=4,r=1.35,$fn=8);
        }
        color(bh_slate(seed)) translate([0,0,h+2.6]) cylinder(h=4.2,r1=2.1,r2=0.05,$fn=8);
    }
    translate([L*0.25,D*0.1,h+rise*0.6]) bh_part_chimney(seed,rise*0.5+1.2);
    bh_part_decay(seed,L,D);
}

// 1960s-80s "kostka" family house: cube, brizolit render, low pyramid roof.
module bh_bldg_kostka(seed=0,L=9,D=9)
{
    fh=2.9; h=2*fh; t=0.4; c=bh_brizolit(seed); rise=2.3;
    color(bh_stone(seed)) bh_slab(L,D,0.12);
    color([0.26,0.24,0.20]) translate([0,0,fh]) bh_slab(L-2*t,D-2*t,0.14);
    translate([0,-D/2+t/2,0]) bh_part_facade(seed,L,2,fh,t,c,true,1.1,2.2,[1.5,1.35],false,0.5,pilaster=false,cols=3);
    translate([0,D/2-t/2,0]) rotate([0,0,180]) bh_part_facade(seed+3,L,2,fh,t,c,false,1,2,[1.3,1.3],false,0.5,pilaster=false,cols=2);
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90]) bh_part_facade(seed+5+sx,D-2*t,2,fh,t,c,false,1,2,[1.0,1.2],false,0.5,pilaster=false,cols=1);
    color(bh_rnd(seed,2)==0 ? [0.20,0.19,0.18] : bh_trim(seed)) translate([0,0,h]) bh_slab(L+0.6,D+0.6,0.16);
    translate([0,0,h+0.16]) bh_part_roof(seed,L+0.4,D+0.4,rise,0.2,0.2,1,bh_rnd(seed,2),bh_rnd(seed+1,2)==0);
    color(bh_iron())
    {
        translate([0,-D/2-0.55,fh-0.1]) bh_slab(3.6,1.1,0.12);
        translate([0,-D/2-1.07,fh+0.5]) bh_box([3.6,0.04,0.04]);
        for(x=[-1.75:0.35:1.75]) translate([x,-D/2-1.07,fh+0.25]) bh_box([0.03,0.03,0.5]);
        translate([L*0.2,D*0.1,h+rise]) { cylinder(h=1.8,r=0.03,$fn=4); for(z=[1.2,1.5,1.75]) translate([0,0,z]) bh_box([1.0,0.03,0.03]); }
    }
    bh_part_decay(seed,L,D);
}

// Weekend chata: plank cottage on a stone base, or an A-frame (áčko).
module bh_bldg_chata(seed=0,L=6,D=5,aframe=-1)
{
    a=aframe>=0 ? aframe==1 : bh_rnd(seed,3)==0;
    w=bh_wood(seed); t=0.22;
    color(bh_stone(seed)) bh_slab(L,D,0.12);
    if(a)
    {
        rise=L*0.95;
        rotate([0,0,90]) bh_part_roof(seed,D,L,rise,0.25,0.25,0,bh_rnd(seed,2),bh_rnd(seed+1,2)==0);
        translate([0,-D/2+t/2,0]) bh_part_gable(seed,L,rise,t,[],"boards",0,1,[0.9,0.9],[1.0,2.05]);
        translate([0,D/2-t/2,0]) rotate([0,0,180]) bh_part_gable(seed+1,L,rise,t,[],"boards",0,1);
        color(bh_frame()) for(sx=[-1,1]) translate([sx*0.52,-D/2+t/2,1.02]) bh_box([0.06,t+0.04,2.05]);
    }
    else
    {
        h=2.5; rise=D*0.55;
        translate([0,-D/2+t/2,0]) bh_part_facade(seed,L,1,h,t,w,true,0.95,2.05,[0.9,0.9],true,0.45,pilaster=false,cols=3,sill=0.9);
        translate([0,D/2-t/2,0]) rotate([0,0,180]) bh_part_facade(seed+2,L,1,h,t,w,false,1,2,[0.8,0.8],false,0,pilaster=false,cols=1);
        for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90])
        {
            bh_part_wall(D-2*t,h,[[0,0.9,0.7,0.8]],t,w);
            translate([0,0,h]) bh_part_gable(seed+sx,D,rise,t,[],"boards",0,0);
        }
        color(bh_wood(seed+3)) for(z=[0.3:0.5:0.9]) for(sy=[-1,1]) translate([0,sy*(D/2+0.01),z]) bh_box([L+0.12,0.03,0.05]);
        translate([0,0,h]) bh_part_roof(seed,L,D,rise,0.4,0.4,0,bh_rnd(seed,2),bh_rnd(seed+1,3)==0);
        color(bh_wood(seed+5))
        {
            translate([0,-D/2-0.9,0]) bh_slab(L*0.7,1.6,0.12);
            for(sx=[-1,1]) translate([sx*L*0.33,-D/2-1.6,0]) bh_box([0.1,0.1,1.0]);
            translate([0,-D/2-1.6,0.45]) bh_box([L*0.7,0.06,0.08]);
        }
    }
}

// JZD cowshed (kravín): long, low, many small high windows, corrugated eternit
// roof with a ventilation lantern; partially collapsed.
module bh_bldg_cowshed(seed=0,L=36,D=11)
{
    h=3.6; t=0.45; c=[[0.40,0.36,0.27],[0.37,0.34,0.30],[0.38,0.29,0.22]][bh_rnd(seed,3)]; rise=1.9;
    color([0.30,0.30,0.28]) bh_slab(L,D,0.14);
    translate([0,-D/2+t/2,0]) bh_part_facade(seed,L,1,h,t,c,true,2.8,2.9,[1.2,0.75],false,0.5,pilaster=false,sill=1.95);
    translate([0,D/2-t/2,0]) rotate([0,0,180]) bh_part_facade(seed+3,L,1,h,t,c,true,2.8,2.9,[1.2,0.75],false,0.5,pilaster=false,sill=1.95);
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90])
    {
        bh_part_wall(D-2*t,h,[[0,0,2.6,2.8]],t,c);
        translate([0,0,h]) bh_part_gable(seed+sx,D,rise,t,c,"plain",0,0);
    }
    translate([0,0,h]) bh_part_roof(seed,L,D,rise,0.4,0.5,0,3,true);
    color(bh_eternit(seed+4)) translate([0,0,h+rise+0.15])
    {
        for(sy=[-1,1]) translate([0,sy*0.45,0]) bh_box([L*0.8,0.06,0.6]);
        for(sy=[-1,1]) translate([0,sy*0.35,0.35]) rotate([-sy*18,0,0]) bh_box([L*0.8,0.8,0.05]);
    }
    color(bh_iron())
    {
        for(sy=[-1,1]) for(x=[-L/2+1.5:2.0:L/2-1.5]) translate([x,sy*(D/2-2.4),0.6]) bh_box([0.06,0.06,1.1]);
        for(sy=[-1,1]) translate([0,sy*(D/2-2.4),1.1]) bh_box([L-3,0.05,0.05]);
    }
    bh_part_decay(seed,L,D);
}

// Brick factory hall (porcelain works): pilasters, segmental windows, stepped gables.
module bh_bldg_factory(seed=0,L=30,D=16)
{
    h=8; t=0.6; b=bh_brick(seed); rise=3.2;
    n=floor(L/4.2); step=L/n;
    color([0.30,0.29,0.27]) bh_slab(L,D,0.14);
    for(sy=[-1,1]) translate([0,sy*(D/2-t/2),0]) rotate([0,0,sy<0 ? 0 : 180])
    {
        ops=[for(i=[0:n-1]) let(x=-L/2+(i+0.5)*step) sy<0 && i==floor(n/2) ? [x,0,3.4,4.2] : [x,1.6,2.3,4.0]];
        bh_part_wall(L,h,ops,t,b);
        color(bh_brick(seed+5)) for(i=[0:n]) translate([-L/2+i*step,-t/2-0.12,h/2]) bh_box([0.7,0.24,h]);
        color(bh_stone(seed)) { translate([0,-t/2-0.1,h-0.3]) bh_box([L+0.3,0.3,0.4]); translate([0,-t/2-0.06,0.5]) bh_box([L,0.12,1.0]); }
        color(bh_frame()) for(o=ops) if(o[1]>0) for(k=[-1,0,1]) translate([o[0]+k*o[2]/3,0,o[1]+o[3]/2]) bh_box([0.06,0.08,o[3]]);
        color(bh_brick(seed+9)) for(o=ops) translate([o[0],-t/2-0.05,o[1]+o[3]+0.18]) bh_box([o[2]+0.5,0.1,0.36]);
    }
    for(sx=[-1,1]) translate([sx*(L/2-t/2),0,0]) rotate([0,0,sx*90])
    {
        bh_part_wall(D-2*t,h,[[0,0,3.2,4.0]],t,b);
        color(b) for(k=[0:4]) translate([0,0,h+k*rise/5]) bh_slab((D+0.2)*(1-k/5),t,rise/5+0.02);
    }
    translate([0,0,h]) bh_part_roof(seed,L,D,rise,0.2,0.3,0,3,true);
    color([0.12,0.14,0.15]) translate([0,0,h+rise-0.6]) for(sy=[-1,1]) translate([0,sy*0.9,0]) rotate([sy*-20,0,0]) bh_box([L*0.7,1.4,0.05]);
    bh_part_decay(seed,L,D);
}

// Hill castle: gate tower on -y (the walk-in entrance), keep at the front-left
// corner and a palace wing along +y facing the courtyard.
module bh_bldg_castle(seed=0,L=34,D=26)
{
    wt=1.4; ch=9; s=bh_stone(seed);
    color(bh_stone(seed+1)) bh_slab(L,D,0.14);
    for(sx=[-1,1]) translate([sx*(L/4+2),-D/2+wt/2,0]) { color(s) bh_slab(L/2-4,wt,ch); color(s) translate([0,0,ch]) bh_part_crenel(L/2-4,wt); }
    translate([0,-D/2+3,0])
    {
        color(bh_stone(seed+2)) difference()
        {
            bh_slab(8,6,14);
            translate([0,0,-0.1]) bh_slab(3.6,6.4,4.4);
            translate([0,0,4.5]) bh_slab(6.6,4.6,9.4);
            for(z=[6.5,10]) translate([0,-3,z]) bh_box([0.7,1,1.3]);
        }
        translate([0,0,14]) bh_part_roof(seed,8,6,3.4,0.3,0.4,1,0);
        color(bh_wood(seed)) translate([-1.6,-3.2,2.0]) rotate([0,0,-80]) translate([0.8,0,0]) bh_box([1.7,0.12,4.0]);
    }
    for(sx=[-1,1]) translate([sx*(L/2-wt/2),0,0]) { color(s) bh_slab(wt,D,ch); color(s) translate([0,0,ch]) rotate([0,0,90]) bh_part_crenel(D,wt); }
    pd=8.5; ph=3*3.4;
    translate([2,D/2-pd/2,0])
    {
        translate([0,-pd/2+0.3,0]) bh_part_facade(seed,L-6,3,3.4,0.6,bh_plaster(seed+4),true,1.8,2.8,[1.0,1.6],false,1.0,true);
        translate([0,pd/2-0.3,0]) rotate([0,0,180]) bh_part_wall(L-6,ph,[],0.6,s);
        for(sx=[-1,1]) translate([sx*((L-6)/2-0.3),0,0]) rotate([0,0,sx*90]) bh_part_wall(pd-1.2,ph,[],0.6,s);
        for(sx=[-1,1]) translate([sx*((L-6)/2-0.3),0,ph]) rotate([0,0,sx*90]) bh_part_gable(seed+sx,pd,pd*0.75,0.6,s,"plain",0,1);
        translate([0,0,ph]) bh_part_roof(seed+3,L-6,pd,pd*0.75,0.3,0.5,0,2);
        color([0.26,0.24,0.20]) for(f=[1:2]) translate([0,0,f*3.4-0.14]) bh_slab(L-7.2,pd-1.2,0.14);
    }
    kw=8.5; kh=26;
    translate([-L/2+kw/2,-D/2+kw/2,0])
    {
        color(s) difference()
        {
            bh_slab(kw,kw,kh);
            translate([0,0,1.5]) bh_slab(kw-3,kw-3,kh);
            for(z=[9,14,19,23]) for(a=[0,90,180,270]) rotate([0,0,a]) translate([0,-kw/2,z]) bh_box([0.6,3.2,1.2]);
        }
        color(s) translate([0,0,kh]) for(a=[0:90:270]) rotate([0,0,a]) translate([0,-kw/2+0.3,0]) for(x=[-kw/2+0.6:1.6:kw/2-0.4]) translate([x,0,0]) bh_slab(0.8,0.6,0.9);
        translate([0,0,kh+0.1]) bh_part_roof(seed+5,kw-1.4,kw-1.4,4.6,0.2,0.2,1,0);
    }
    bh_part_decay(seed,L*0.5,D*0.5);
}

// ================= Props =================
// Farmyard wall along x with an arched cart gate at x=0 and a side wicket.
module bh_prop_yard_wall(seed=0,L=10,h=2.5,gate=3.4)
{
    t=0.5; c=bh_rnd(seed,3)==0 ? bh_stone(seed) : bh_plaster(seed);
    hasGate=gate>0 && L>gate+3;
    ops=hasGate ? [[0,0,gate+0.4,h],[gate/2+1.25,0,0.95,min(2.0,h-0.3)]] : [];
    bh_part_wall(L,h,ops,t,c);
    color(bh_tile(seed+1)) for(r=hasGate ? [[-L/2,-gate/2-0.6],[gate/2+0.6,L/2]] : [[-L/2,L/2]])
        translate([(r[0]+r[1])/2,0,h+0.05]) rotate([0,90,0]) cylinder(h=r[1]-r[0],r=0.36,center=true,$fn=3);
    if(hasGate)
    {
        sp=2.6; top=sp+gate/2+0.5;
        color(c) for(sx=[-1,1]) translate([sx*(gate/2+0.3),0,top/2]) bh_box([0.6,t+0.1,top]);
        color(c) translate([0,0,sp]) difference()
        {
            translate([0,0,(top-sp)/2]) bh_box([gate+1.2,t+0.1,top-sp]);
            rotate([90,0,0]) cylinder(h=t+0.4,r=gate/2,center=true,$fn=20);
        }
        color(bh_trim(seed)) translate([0,-t/2-0.06,sp]) difference()
        {
            rotate([90,0,0]) cylinder(h=0.08,r=gate/2+0.22,center=true,$fn=20);
            rotate([90,0,0]) cylinder(h=0.3,r=gate/2,center=true,$fn=20);
            translate([0,0,-gate/2-0.3]) bh_box([gate+1,1,gate+0.6]);
        }
        color(bh_tile(seed)) translate([0,0,top+0.05]) rotate([0,90,0]) cylinder(h=gate+1.6,r=0.48,center=true,$fn=3);
        color(bh_wood(seed)) translate([gate/2-0.1,0.25,1.4]) rotate([0,0,96]) translate([-gate/4,0,0]) bh_box([gate/2,0.08,2.7]);
    }
    for(i=[0:2]) if(bh_rnd(seed+i*7,2)==0) color(bh_stone(seed+i))
        translate([bh_rf(seed+i,-L/2+0.6,L/2-0.6),-t/2-0.01,bh_rf(seed+i*3,0.3,h-0.5)]) bh_box([bh_rf(seed+i*5,0.5,1.1),0.02,0.4]);
}

// Wooden picket fence (plaňkový plot) along x; gaps and leaning sections.
module bh_prop_fence(seed=0,L=10)
{
    w=bh_wood(seed);
    color(w)
    {
        for(x=[-L/2:2.4:L/2]) translate([x,0,0.7]) bh_box([0.1,0.1,1.4]);
        for(z=[0.35,1.05]) translate([0,0.06,z]) bh_box([L,0.05,0.08]);
        for(x=[-L/2+0.08:0.16:L/2]) if(bh_rnd(seed+round(x*17),6)!=0)
            translate([x,-0.01,0.62]) rotate([0,bh_rnd(seed+round(x*29),9)==0 ? 14 : 0,0]) bh_box([0.08,0.025,1.15]);
    }
}

// Baroque plague column (morový sloup) with the Virgin on top.
module bh_prop_plague_column(seed=0)
{
    s=bh_stone(seed+1); g=[0.40,0.37,0.30];
    color(s) for(k=[0:2]) translate([0,0,k*0.3]) rotate([0,0,22.5]) cylinder(h=0.3,r=3.2-k*0.55,$fn=8);
    color(g) translate([0,0,0.9]) bh_slab(1.8,1.8,2.4);
    color(s) translate([0,0,3.2]) bh_slab(2.2,2.2,0.3);
    color(g) for(sx=[-1,1],sy=[-1,1]) translate([sx*0.95,sy*0.95,3.5]) { cylinder(h=0.9,r1=0.22,r2=0.14,$fn=6); translate([0,0,1.0]) sphere(r=0.16,$fn=6); }
    color(g) translate([0,0,3.5]) { cylinder(h=7.2,r1=0.48,r2=0.36,$fn=10); translate([0,0,7.2]) bh_slab(1.1,1.1,0.4); }
    color(g) translate([0,0,11.1])
    {
        cylinder(h=1.5,r1=0.42,r2=0.2,$fn=8);
        translate([0,0,1.62]) sphere(r=0.2,$fn=8);
        translate([0,0,1.66]) rotate_extrude($fn=12) polygon([[0.34,0],[0.40,0],[0.40,0.05],[0.34,0.05]]);
    }
}

// Octagonal stone fountain basin (kašna), long dry.
module bh_prop_fountain(seed=0,r=3.0)
{
    color(bh_stone(seed)) difference()
    {
        rotate([0,0,22.5]) cylinder(h=0.85,r=r,$fn=8);
        translate([0,0,0.25]) rotate([0,0,22.5]) cylinder(h=1,r=r-0.32,$fn=8);
    }
    color([0.20,0.20,0.16]) translate([0,0,0.25]) rotate([0,0,22.5]) cylinder(h=0.04,r=r-0.33,$fn=8);
    color(bh_stone(seed+1)) { cylinder(h=2.4,r1=0.35,r2=0.25,$fn=8); translate([0,0,2.2]) cylinder(h=0.25,r1=0.4,r2=0.85,$fn=8); }
}

// War memorial (pomník padlým): obelisk on steps behind four chained posts.
module bh_prop_memorial(seed=0)
{
    s=bh_stone(seed);
    color(s) { bh_slab(2.6,2.6,0.3); translate([0,0,0.3]) bh_slab(1.6,1.6,1.2); translate([0,0,1.5]) rotate([0,0,45]) cylinder(h=4.2,r1=0.7,r2=0.25,$fn=4); }
    color([0.12,0.12,0.11]) translate([0,-0.81,0.95]) bh_box([1.0,0.03,0.6]);
    color(bh_iron()) for(sx=[-1,1],sy=[-1,1]) translate([sx*1.8,sy*1.8,0]) cylinder(h=0.9,r=0.06,$fn=6);
    color(bh_iron()) for(a=[0:90:270]) rotate([0,0,a]) translate([0,-1.8,0.75]) rotate([0,90,0]) cylinder(h=3.6,r=0.02,center=true,$fn=4);
}

// Wayside cross on a stone base (kříž u cesty).
module bh_prop_cross(seed=0)
{
    color(bh_stone(seed)) { bh_slab(1.0,1.0,0.35); translate([0,0,0.35]) bh_slab(0.6,0.6,1.3); }
    color(bh_rnd(seed,2)==0 ? bh_iron() : bh_stone(seed+1)) translate([0,0,1.65]) { translate([0,0,1.1]) bh_box([0.12,0.1,2.2]); translate([0,0,1.6]) bh_box([0.9,0.1,0.12]); }
}

// Pillar shrine (boží muka): niche under a small tile roof.
module bh_prop_shrine(seed=0)
{
    c=bh_plaster(seed);
    color(c) { bh_slab(0.7,0.7,2.2); translate([0,0,2.2]) bh_slab(0.95,0.95,0.9); }
    color(bh_dark()) translate([0,-0.45,2.65]) bh_box([0.45,0.08,0.6]);
    color(bh_tile(seed)) translate([0,0,3.1]) rotate([0,0,45]) cylinder(h=0.9,r1=0.85,r2=0.03,$fn=4);
    color(bh_iron()) translate([0,0,3.95]) { bh_box([0.05,0.05,0.5]); translate([0,0,0.1]) bh_box([0.3,0.05,0.05]); }
}

// Village bus shelter (zastávka): brick back, tile mono-pitch, bench.
module bh_prop_bus_shelter(seed=0)
{
    c=bh_rnd(seed,2)==0 ? bh_brick(seed) : bh_plaster(seed);
    color(c) { translate([0,0.9,1.2]) bh_box([4.4,0.3,2.4]); for(sx=[-1,1]) translate([sx*2.05,0.3,1.2]) bh_box([0.3,1.5,2.4]); }
    color(bh_tile(seed)) translate([0,0.25,2.55]) rotate([-12,0,0]) bh_box([4.9,2.3,0.12]);
    color(bh_wood(seed)) { translate([0,0.55,0.45]) bh_box([3.6,0.4,0.06]); for(sx=[-1,1]) translate([sx*1.6,0.55,0.22]) bh_box([0.08,0.36,0.44]); }
    color(bh_iron()) translate([2.6,-0.6,0]) { cylinder(h=2.6,r=0.04,$fn=6); color([0.47,0.42,0.12]) translate([0,0,2.4]) bh_box([0.5,0.04,0.4]); }
}

// Village PA loudspeaker (obecní rozhlas) and lamp on a concrete pole.
module bh_prop_pole(seed=0,h=7.5)
{
    color([0.38,0.37,0.35]) rotate([0,0,45]) cylinder(h=h,r1=0.14,r2=0.08,$fn=4);
    color([0.30,0.31,0.29]) translate([0,0,h-1.4]) for(a=[0,150]) rotate([0,0,a]) translate([0,-0.22,0]) rotate([100,0,0]) cylinder(h=0.55,r1=0.06,r2=0.24,$fn=8);
    color(bh_iron()) translate([0,0,h-0.4]) { rotate([0,90,0]) cylinder(h=1.2,r=0.03,$fn=4); translate([1.15,0,-0.15]) cylinder(h=0.15,r1=0.25,r2=0.08,$fn=8); }
}

// Round hay bale, wrapped in white film (or bare straw), lying on its side.
module bh_prop_bale(seed=0)
{
    color(bh_rnd(seed,3)==0 ? [0.47,0.40,0.24] : bh_rnd(seed,3)==1 ? [0.44,0.44,0.42] : [0.36,0.37,0.31])
        translate([0,0,0.72]) rotate([90,0,bh_rnd(seed,180)]) cylinder(h=1.2,r=0.72,center=true,$fn=12);
}

// Twin concrete silos with a top house (JZD).
module bh_prop_silo(seed=0,h=16)
{
    color([0.39,0.38,0.35]) for(sx=[-1,1]) translate([sx*2.4,0,0]) cylinder(h=h,r=2.3,$fn=14);
    color([0.34,0.33,0.31]) translate([0,0,h]) bh_slab(9.6,4.0,2.4);
    color(bh_eternit(seed)) translate([0,0,h+2.4]) bh_slab(10,4.4,0.2);
    color([0.24,0.18,0.14]) for(sx=[-1,1]) translate([sx*2.4,-2.31,h*0.5]) bh_box([0.5,0.04,h*0.9]);
}

// Tapered brick factory chimney on a square plinth.
module bh_prop_chimney(seed=0,h=34)
{
    color(bh_brick(seed)) { bh_slab(3.6,3.6,3.2); translate([0,0,3.2]) cylinder(h=h-3.2,r1=1.55,r2=0.95,$fn=12); }
    color(bh_brick(seed+1)) for(z=[h*0.5,h-1.2]) translate([0,0,z]) cylinder(h=0.5,r=1.55-(1.55-0.95)*(z-3.2)/(h-3.2)+0.12,$fn=12);
    color(bh_dark()) translate([0,0,h-0.05]) cylinder(h=0.1,r=0.7,$fn=12);
}

// ================= Flora (single meshes; scale outside for variety) =================
// Norway spruce: tall, narrow, drooping tiers, blue-black green.
module bh_nature_spruce(seed=0,h=20)
{
    n=7; cb=h*0.22;
    color([0.24,0.18,0.13]) cylinder(h=h*0.95,r1=h*0.017,r2=0.04,$fn=5);
    for(i=[0:n-1]) let(f=i/n,r=h*0.13*(1-f*0.88)+0.25,z=cb+(h-cb)*f)
        color(i%2==0 ? [0.09,0.16,0.11] : [0.11,0.19,0.12]) translate([0,0,z-0.2]) rotate([0,0,i*29+bh_rnd(seed+i,30)])
            cylinder(h=(h-cb)/n*1.75,r1=r,r2=r*0.28,$fn=7);
}

// Bark-beetle spruce (kůrovec): red-stage crown or grey bare snag.
module bh_nature_spruce_dead(seed=0,h=18)
{
    red=bh_rnd(seed,2)==0;
    color([0.33,0.30,0.27]) cylinder(h=h,r1=h*0.017,r2=0.03,$fn=6);
    for(i=[0:8]) let(z=h*0.28+i*h*0.075,r=h*0.11*(1-i/10))
    {
        color([0.29,0.26,0.23]) for(a=[0:120:240]) rotate([0,0,a+i*41]) translate([0,0,z]) rotate([0,104,0]) cylinder(h=r,r1=0.05,r2=0.01,$fn=4);
        if(red && i<7) color([0.30,0.19,0.12]) translate([0,0,z-0.4]) rotate([0,0,i*29]) cylinder(h=1.3,r1=r*0.9,r2=r*0.3,$fn=6);
    }
}

// Scots pine: tall bare orange trunk, flat clumped crown.
module bh_nature_pine(seed=0,h=18)
{
    lean=bh_rf(seed,-5,5);
    rotate([lean,bh_rf(seed+1,-4,4),0])
    {
        color([0.38,0.22,0.13]) cylinder(h=h*0.86,r1=h*0.02,r2=h*0.008,$fn=6);
        for(i=[0:4]) color(i%2==0 ? [0.14,0.22,0.12] : [0.17,0.25,0.13])
            translate([bh_rf(seed+i*3,-1.8,1.8),bh_rf(seed+i*5,-1.8,1.8),h*(0.72+i*0.05)]) scale([1,1,0.45]) sphere(r=h*0.11,$fn=7);
    }
}

// Broadleaf (beech/oak): smooth trunk, several limbs, lumpy rounded crown.
module bh_nature_broadleaf(seed=0,h=16,oak=false)
{
    tc=oak ? [0.26,0.22,0.17] : [0.36,0.35,0.32];
    color(tc) cylinder(h=h*0.45,r1=h*0.032,r2=h*0.022,$fn=7);
    for(i=[0:3]) color(tc) translate([0,0,h*0.38]) rotate([0,32+bh_rnd(seed+i,15),i*90+bh_rnd(seed+i*3,40)]) cylinder(h=h*0.28,r1=h*0.018,r2=h*0.006,$fn=5);
    for(i=[0:5]) let(a=i*60+bh_rnd(seed+i,30),d=i==0 ? 0 : h*bh_rf(seed+i*7,0.14,0.24))
        color(oak ? [0.14+0.02*(i%2),0.21,0.09] : [0.17+0.03*(i%2),0.26,0.11])
            translate([d*cos(a),d*sin(a),h*bh_rf(seed+i*11,0.62,0.8)]) scale([1,1,0.8]) sphere(r=h*bh_rf(seed+i*13,0.16,0.22),$fn=8);
}

// Small-leaved linden (lípa): dense dome, the village-green and chapel tree.
module bh_nature_linden(seed=0,h=17)
{
    color([0.27,0.23,0.18]) cylinder(h=h*0.4,r1=h*0.04,r2=h*0.03,$fn=7);
    color([0.17,0.26,0.10]) translate([0,0,h*0.62]) scale([1,1,1.15]) sphere(r=h*0.27,$fn=10);
    for(i=[0:4]) let(a=i*72+bh_rnd(seed+i,30)) color([0.19,0.29,0.11])
        translate([h*0.16*cos(a),h*0.16*sin(a),h*bh_rf(seed+i,0.5,0.78)]) sphere(r=h*0.16,$fn=8);
}

// Old fruit tree (apple/plum/cherry): short gnarled trunk, low wide crown.
module bh_nature_fruit(seed=0,h=5.5)
{
    dead=bh_rnd(seed,6)==0;
    color([0.25,0.20,0.15]) { cylinder(h=h*0.3,r1=0.16,r2=0.11,$fn=6); for(i=[0:3]) translate([0,0,h*0.27]) rotate([0,40+bh_rnd(seed+i,20),i*90+bh_rnd(seed+i*5,30)]) cylinder(h=h*0.4,r1=0.08,r2=0.02,$fn=4); }
    if(!dead) for(i=[0:4]) let(a=i*72+bh_rnd(seed+i*3,40)) color([0.19+0.02*(i%2),0.27,0.11])
        translate([h*0.2*cos(a),h*0.2*sin(a),h*bh_rf(seed+i*7,0.55,0.75)]) scale([1,1,0.75]) sphere(r=h*bh_rf(seed+i,0.2,0.26),$fn=7);
}

// Riparian alder: two or three stems, narrow dark oval crowns.
module bh_nature_alder(seed=0,h=13)
{
    for(i=[0:1+bh_rnd(seed,2)]) let(x=bh_rf(seed+i*3,-0.7,0.7),y=bh_rf(seed+i*5,-0.7,0.7),hh=h*bh_rf(seed+i,0.8,1))
        translate([x,y,0]) rotate([bh_rf(seed+i,-6,6),bh_rf(seed+i*7,-6,6),0])
        {
            color([0.25,0.23,0.20]) cylinder(h=hh*0.75,r1=0.18,r2=0.06,$fn=6);
            color([0.15,0.23,0.11]) translate([0,0,hh*0.62]) scale([0.8,0.8,1.6]) sphere(r=hh*0.17,$fn=8);
        }
}

// Pollarded willow (hlavatá vrba): squat trunk, knobbed head, upright rods.
module bh_nature_willow(seed=0,h=7)
{
    color([0.28,0.25,0.21]) { cylinder(h=2.3,r1=0.42,r2=0.36,$fn=7); translate([0,0,2.3]) scale([1,1,0.6]) sphere(r=0.55,$fn=7); }
    for(i=[0:7]) let(a=i*45+bh_rnd(seed+i,30)) color([0.31,0.36,0.22])
        translate([0.3*cos(a),0.3*sin(a),2.4]) rotate([0,10+bh_rnd(seed+i,15),a]) { cylinder(h=h-2.6,r1=0.06,r2=0.01,$fn=4); translate([0,0,(h-2.6)*0.6]) scale([0.5,0.5,1.4]) sphere(r=0.75,$fn=6); }
}

// Silver birch: slender white trunk, light open crown.
module bh_nature_birch(seed=0,h=14)
{
    color([0.45,0.44,0.41]) cylinder(h=h*0.8,r1=h*0.016,r2=h*0.006,$fn=6);
    color(bh_dark()) for(i=[0:5]) translate([0,0,h*(0.08+i*0.11)]) rotate([0,0,i*67]) translate([h*0.012,0,0]) bh_box([0.1,0.1,0.12]);
    for(i=[0:4]) let(a=i*72+bh_rnd(seed+i,40)) color([0.22+0.02*(i%2),0.30,0.12])
        translate([h*0.08*cos(a),h*0.08*sin(a),h*bh_rf(seed+i*3,0.55,0.86)]) scale([0.8,0.8,1.3]) sphere(r=h*0.11,$fn=7);
}

// Hedgerow shrub (blackthorn / hawthorn / elder).
module bh_nature_hedge(seed=0,r=2.2)
{
    for(i=[0:4]) color(i%2==0 ? [0.15,0.22,0.10] : [0.19,0.26,0.12])
        translate([bh_rf(seed+i*3,-r*0.6,r*0.6),bh_rf(seed+i*5,-r*0.5,r*0.5),r*0.38]) scale([1,1,0.75]) sphere(r=r*bh_rf(seed+i*7,0.35,0.55),$fn=7);
}

// Low forest-floor undergrowth: fern / raspberry clump.
module bh_nature_fern(seed=0)
{
    for(i=[0:6]) color(i%2==0 ? [0.20,0.30,0.12] : [0.25,0.33,0.14])
        rotate([0,0,i*51+bh_rnd(seed+i,30)]) translate([0.35,0,0.05]) rotate([0,-55,0]) scale([1,0.3,0.05]) translate([0.45,0,0]) sphere(r=0.5,$fn=6);
}

// Fallow-field weeds: dry stalks with tufts, occasional feral rapeseed yellow.
module bh_nature_weeds(seed=0)
{
    yellow=bh_rnd(seed,4)==0;
    for(i=[0:6]) let(x=bh_rf(seed+i*3,-0.6,0.6),y=bh_rf(seed+i*5,-0.6,0.6),hh=bh_rf(seed+i*7,0.7,1.3))
        translate([x,y,0])
        {
            color([0.40,0.36,0.20]) cylinder(h=hh,r1=0.025,r2=0.01,$fn=4);
            color(yellow ? [0.50,0.43,0.08] : [0.36,0.31,0.18]) translate([0,0,hh]) scale([1,1,0.6]) sphere(r=0.13,$fn=5);
        }
}
