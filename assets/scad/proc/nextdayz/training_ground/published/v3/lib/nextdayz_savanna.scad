// nextdayz_savanna.scad — East African savanna region for geographic NextDayz
// maps: settlement plans, furnished buildings, compound furniture, flora and
// land cover. Same item protocol as nextdayz_settlement.scad; shells and plants
// come from kit_savanna.scad (sv_).
//
// Settlement kinds (site = [id,name,kind,center,size,rotation,buildings,risk]):
//   sv_trading  duka street of a trading centre: shop-houses with verandas on
//               both sides of a murram road, market stalls, a borehole tank
//   sv_plots    peri-urban plots: scattered iron and mud houses in swept yards
//   sv_boma     Maasai enkang: loaf huts in a ring inside a thorn fence, the
//               cattle kraal in the middle, one gate
//   sv_shamba   farm homestead: mabati house, kitchen huts, granary, calf pen
//   sv_mission  church and primary school around an assembly ground
//   sv_clinic   dispensary compound with staff house and water tower
//   sv_gate     reserve gate: arch and boom on the road, warden's office,
//               Uniport ranger lines, store, radio mast
//   sv_post     police / ranger outpost: the same lines without the gate
//   sv_camp     tented safari camp: a thatched mess banda behind a row of
//               tents that face the site's -y edge (turn it to the river)
use <kit_savanna.scad>
use <kit_coldwar.scad>
use <nextdayz_coldwar.scad>

function sv_is_settlement(k) = k=="sv_trading" || k=="sv_plots" || k=="sv_boma" || k=="sv_shamba" || k=="sv_mission" ||
    k=="sv_clinic" || k=="sv_gate" || k=="sv_post" || k=="sv_camp";
function sv_u(s,k) = sv_rnd(s*7+k*131,1000)/999;
function sv_gap() = 12;
function sv_sum(v,i=0) = i>=len(v) ? 0 : v[i]+sv_sum(v,i+1);
function sv_cum(v,i) = i<=0 ? 0 : v[i-1]+sv_cum(v,i-1);

// Row of m detached fronts across length Lr: [[centre offset, width], ...];
// widths vary wmin..wmax and a narrow alley stays between neighbours.
function sv_row(m,Lr,seed,wmin=6,wmax=9.5,alley=1.4) = m<=0 ? [] : let(
    base=(Lr-(m-1)*alley)/m,
    w0=[for(i=[0:m-1]) min(wmax,max(wmin,base*(0.85+0.3*sv_u(seed,i))))],
    T=sv_sum(w0), k=T>Lr-(m-1)*alley ? (Lr-(m-1)*alley)/T : 1,
    w=[for(x=w0) x*k], gap=m<2 ? 0 : (Lr-sv_sum(w))/(m-1))
    [for(i=[0:m-1]) [-Lr/2+sv_cum(w,i)+i*gap+w[i]/2,w[i]]];

// --- Trading centre: one murram street, dukas on both sides. ---
function sv_trade_kind(i,m,seed,row) = let(u=sv_u(seed,i*3+row))
    row==0 && i==0 ? "bar" : row==1 && i==m-1 ? "chemist" : row==1 && i==0 ? "store" :
    u<0.16 ? "butchery" : u<0.34 ? "sv_house" : "duka";
function sv_m_trading(W,H,n,risk,seed) = let(
    Db=8, S=max(1,round(W/75)), gap=sv_gap(), secL=(W-2-(S-1)*gap)/S,
    rows=2*S, per=[for(r=[0:rows-1]) floor(n/rows)+(r<n%rows ? 1 : 0)],
    yf=7+Db/2)
    concat(
        [for(s=[0:S-1]) ["pad",-W/2+1+secL/2+s*(secL+gap),0,0,secL,H-2,"road",3.5]],
        [["road",[[-W/2+1,0],[W/2-1,0]],8]],
        [for(r=[0:rows-1]) let(s=floor(r/2),sd=r%2==0 ? 1 : -1,row=sv_row(per[r],secL-3,seed+r*29),
                                 x0=-W/2+1+secL/2+s*(secL+gap))
            if(per[r]>0) for(i=[0:len(row)-1])
                ["bldg",x0+sd*row[i][0],sd*yf,sd>0 ? 0 : 180,sv_trade_kind(i,len(row),seed+r,r%2),row[i][1],Db,[]]],
        [for(s=[0:S-1]) let(x0=-W/2+1+secL/2+s*(secL+gap)) each
            [["prop",x0-secL*0.3,2.6,0,"sv_stall",0,0],["prop",x0-secL*0.12,-2.8,180,"sv_stall",0,0],
             ["prop",x0+secL*0.22,3.2,20,"sv_boda",0,0],["prop",x0+secL*0.05,2.2,0,"sv_sacks",0,0],
             ["prop",x0+secL*0.36,-3.0,0,"sv_jerrycans",0,0],["prop",x0-secL*0.42,-3.6,0,"sv_pole",0,0],
             ["prop",x0+secL*0.3,-2.4,170,s%2==0 ? "truck" : "car",0,0],
             ["zed",x0+secL*0.1,0.5,0,"civilian",1+risk],["zed",x0-secL*0.2,-1,0,"civilian",1+risk],
             ["loot",x0-secL*0.3,2.6,0,"commercial"]]],
        [["prop",-W/2+5,H/2-6,0,"sv_tank_tower",0,0],["prop",-W/2+8.5,H/2-6,0,"pump",0,0],
         ["prop",W/2-5,-H/2+5,0,"sv_latrine",0,0],["plant",W/2-6,H/2-6,0,"acacia",1.0]]);

// --- Peri-urban plots: houses scattered on swept yards. ---
function sv_m_plots(W,H,n,risk,seed) = let(
    cols=max(1,ceil(sqrt(n*W/H))), rows=ceil(n/cols), cw=W/cols, ch=H/rows, P=15,
    at=[for(i=[0:n-1]) let(c=i%cols,r=floor(i/cols))
        [-W/2+(c+0.5)*cw+(sv_u(seed,i)-0.5)*max(0,cw-P-sv_gap()),-H/2+(r+0.5)*ch+(sv_u(seed,i+50)-0.5)*max(0,ch-P-sv_gap()),
         floor(sv_u(seed,i+90)*4)*90+(sv_u(seed,i+120)-0.5)*20]])
    concat(
        [for(i=[0:n-1]) ["pad",at[i][0],at[i][1],at[i][2],P,P,"sand",2.5]],
        [for(i=[0:n-1]) let(hut=sv_u(seed,i+200)<0.22)
            ["bldg",at[i][0],at[i][1],at[i][2],hut ? "rondavel" : "sv_house",hut ? 6 : 7.5+sv_u(seed,i+230)*1.5,6,[]]],
        [for(i=[0:n-1]) let(a=at[i][2]) each concat(
            sv_u(seed,i+300)<0.5 ? [["prop",at[i][0]+5.6*cos(a)+5.8*sin(a),at[i][1]+5.6*sin(a)-5.8*cos(a),a,"sv_latrine",0,0]] : [],
            sv_u(seed,i+330)<0.4 ? [["prop",at[i][0]-5.6*cos(a)+5.6*sin(a),at[i][1]-5.6*sin(a)-5.6*cos(a),a,"sv_jerrycans",0,0]] : [],
            [["plant",at[i][0]-9.5*cos(a+40),at[i][1]-9.5*sin(a+40),0,sv_u(seed,i+360)<0.4 ? "banana" : sv_u(seed,i+360)<0.7 ? "sisal" : "acacia",0.9]])],
        [for(k=[0:risk+floor(n/4)]) ["zed",at[k%n][0]+8,at[k%n][1]-8,0,"civilian",1+risk]],
        [["prop",at[0][0]-8.5,at[0][1]+8.5,0,"pump",0,0],["loot",at[n-1][0]+8.5,at[n-1][1]-8.5,0,"residential"]]);

// --- Maasai enkang: huts in a ring, cattle in the middle, thorn all round. ---
function sv_boma_r(n) = 6.5+n*1.8;
function sv_m_boma(W,H,n,risk,seed) = let(
    R=min(min(W,H)/2-3,sv_boma_r(n)+4), Rh=R-5.6, a0=-50, a1=230)
    concat(
        [["pad",0,0,0,2*R+3,2*R+3,"dry_grass",3]],
        [["prop",0,0,0,"sv_kraal",R,4.0]],
        [for(i=[0:n-1]) let(a=n<2 ? 90 : a0+i*(a1-a0)/(n-1)) ["bldg",Rh*cos(a),Rh*sin(a),a-90,"manyatta",6.4+sv_u(seed,i)*0.6,5,[]]],
        [["prop",0,1,0,"sv_kraal",max(4,Rh-7.5),3.0],["prop",-2,-R*0.55,0,"sv_bones",0,0],
         ["prop",3.5,-R*0.5,0,"sv_jerrycans",0,0],["plant",R+4,R*0.3,0,"acacia",1.1],["plant",-R-5,-R*0.2,0,"balanites",1.0],
         ["loot",0,-R*0.62,0,"farm"],["loot",2,2,0,"farm"]],
        [for(k=[0:risk+floor(n/4)]) ["zed",(k%2==0 ? -1 : 1)*(2+k),-R*0.3-k,0,"civilian",1+risk]]);

// --- Shamba homestead: mabati house, kitchen huts, granary, calf pen. ---
function sv_m_shamba(W,H,n,risk,seed) = let(pw=min(36,W-8), pd=min(34,H-8))
    concat(
        [["pad",0,0,0,pw,pd,"road",3]],
        [["bldg",-pw*0.2,pd/2-5.5,0,"sv_house",8.5,6,[]]],
        n>=2 ? [["bldg",pw/2-5.5,pd*0.1,-90,"rondavel",6,6,[]]] : [],
        n>=3 ? [["bldg",pw/2-5.5,-pd/2+5.5,-90,"rondavel",5.6,5.6,[]]] : [],
        n>=4 ? [["bldg",-pw/2+6.5,-pd/2+6,90,"store",9,6,[]]] : [],
        [["prop",-pw/2+4.5,pd/2-11,0,"sv_granary",0,0],["prop",pw*0.22,pd/2-3,0,"sv_tank",0,0],
         ["prop",-pw/2+2.5,pd/2-2.5,0,"sv_latrine",0,0],["prop",-pw*0.05,-pd/2+4.5,0,"sv_kraal",3.6,2.4],
         ["prop",pw*0.05,pd*0.05,0,"pump",0,0],["prop",-pw*0.3,pd*0.05,30,"sv_jerrycans",0,0],
         ["prop",pw*0.12,-pd*0.2,60,nd_shamba_wreck(seed),0,0]],
        [for(i=[0:4]) ["plant",-pw/2-3,-pd/2+3+i*(pd-6)/4,0,"sisal",1.0]],
        [["plant",pw/2+4,pd/2-3,0,"banana",1.1],["plant",pw/2+5,pd/2-8,40,"banana",0.95],["plant",-pw*0.1,pd/2+6,0,"acacia",1.15]],
        [["loot",0,0,0,"farm"],["zed",pw*0.2,-pd*0.3,0,"civilian",1+risk],["zed",-pw*0.3,pd*0.1,0,"civilian",1]]);
function nd_shamba_wreck(seed) = sv_u(seed,77)<0.5 ? "tractor" : "sv_boda";

// --- Mission: church beside a school around an assembly ground. ---
function sv_m_mission(W,H,n,risk,seed) = let(
    blocks=min(3,max(1,n-1)), houses=n-1-blocks, bl=min(22,W-46), cx=-W/2+10)
    concat(
        [["pad",0,0,0,W-2,H-2,"road",4]],
        [["bldg",cx,-2,0,"church",9,16,[]]],
        [["bldg",12,H/2-7.5,0,"school",bl,7,[]]],
        blocks>=2 ? [["bldg",12,-H/2+7.5,180,"school",bl,7,[]]] : [],
        blocks>=3 ? [["bldg",W/2-7.5,0,-90,"school",min(18,H-44),7,[]]] : [],
        houses>0 ? [for(i=[0:houses-1]) ["bldg",cx+(i%2)*10-2,H/2-6-floor(i/2)*9,0,"sv_house",8,6,[]]] : [],
        [["prop",12,0,0,"sv_flagpole",0,0],["prop",-W/2+24,H/2-5,0,"sv_tank",0,0],["prop",-W/2+24,-H/2+4,0,"sv_latrine",0,0],
         ["prop",-W/2+27,-H/2+4,0,"sv_latrine",0,0],["prop",2,-6,0,"bench",0,0],["prop",22,6,90,"bench",0,0],
         ["prop",-W/2+21,6,0,"pump",0,0],["plant",0,8,0,"acacia",1.2],["plant",24,-7,0,"balanites",1.1]],
        [["loot",12,-3,0,"residential"],["loot",-W/2+21,0,0,"medical"]],
        [for(k=[0:risk+1]) ["zed",4+k*7,(k%2-0.5)*10,0,"civilian",1+risk]]);

// --- Dispensary compound. ---
function sv_m_clinic(W,H,n,risk,seed) = concat(
    [["pad",0,0,0,W-2,H-2,"road",4]],
    [["bldg",-W*0.1,H/2-7.5,0,"clinic",min(20,W-24),7,[]]],
    n>=2 ? [["bldg",W/2-8,-H/2+7.5,180,"sv_house",8.5,6,[]]] : [],
    n>=3 ? [["bldg",-W/2+8.5,-H/2+7.5,180,"store",10,7,[]]] : [],
    n>=4 ? [["bldg",W/2-6.5,H/2-16,-90,"rondavel",6,6,[]]] : [],
    [["prop",W/2-5,H/2-5,0,"sv_tank_tower",0,0],["prop",-W*0.1,H/2-15,10,"ambulance",0,0],["prop",-W*0.3,H/2-13.5,0,"bench",0,0],
     ["prop",0,-H/2+3.5,0,"sv_latrine",0,0],["prop",W*0.2,H/2-13,0,"pump",0,0],["plant",-W/2+5,H*0.1,0,"acacia",1.1],
     ["loot",-W*0.1,H/2-13.5,0,"medical"],["loot",W*0.15,-H*0.1,0,"medical"]],
    [for(k=[0:risk+1]) ["zed",-W*0.25+k*W*0.2,(k%2-0.5)*8,0,"civilian",1+risk]]);

// --- Reserve gate / ranger post: office, Uniport lines, store, mast. ---
function sv_m_gate(W,H,n,risk,seed,arch=true) = let(lines=max(0,n-(n>=3 ? 2 : 1)))
    concat(
        [["pad",0,0,0,W-2,H-2,"road",4]],
        arch ? [["road",[[-W/2+1,-H/2+9],[W/2-1,-H/2+9]],7],["prop",0,-H/2+9,90,"sv_gate_arch",7,0]] : [],
        [["bldg",-W*0.2,H/2-7,0,"ranger",11,8,[]]],
        n>=3 ? [["bldg",W/2-8.5,H/2-7.5,0,"armoury",10,7,[]]] : [],
        lines>0 ? [for(i=[0:lines-1]) ["bldg",-W/2+6+i*8.2,-H/2+(arch ? 22 : 7.5),180,"uniport",5.4,5.4,[]]] : [],
        [["prop",W/2-5,-H/2+(arch ? 20 : 6),0,"sv_mast",0,0],["prop",-W*0.2,H/2-16,0,"sv_flagpole",0,0],
         ["prop",W*0.12,H/2-17,25,"truck",0,0],["prop",W*0.3,-H/2+(arch ? 22 : 9),0,"fueltank",0,0],
         ["prop",W*0.05,-H/2+(arch ? 15 : 4),0,"sandbag",5,0],["prop",-W*0.36,H/2-14,0,"pump",0,0],
         ["loot",W*0.1,H/2-14,0,"military"],["loot",-W*0.3,-H/2+(arch ? 17 : 12),0,"police"]],
        [for(k=[0:risk+1]) ["zed",-W*0.3+k*W*0.2,(k%2)*6-2,0,"military",1+risk]]);

// --- Tented camp: mess banda behind a row of tents that face -y. ---
function sv_m_camp(W,H,n,risk,seed) = let(store=n>=6 ? 1 : 0, tents=n-1-store, pitch=tents>1 ? (W-14)/(tents-1) : 0)
    concat(
        [["pad",0,H/2-8.5,0,18,15,"road",2.5],["bldg",0,H/2-8.5,0,"banda",12,9,[]]],
        store ? [["pad",W/2-9,H/2-7.5,0,14,13,"road",2.5],["bldg",W/2-9,H/2-7,0,"store",9,7,[]]] : [],
        [for(i=[0:tents-1]) let(x=tents>1 ? -W/2+7+i*pitch : 0) each
            [["pad",x,-H/2+8.5,0,8,13,"grass",2.5],["bldg",x,-H/2+9.5,0,"tent",5,7,[]]]],
        [["prop",0,H/2-20,0,"sv_firepit",0,0],["prop",-12,H/2-5,0,"sv_tank",0,0],["prop",-W/2+4,H/2-4,0,"sv_latrine",0,0],
         ["prop",11,H/2-12,-30,"car",0,0],["prop",-10,H/2-12,0,"pump",0,0],
         ["plant",-W/2+5,0,0,"fig",0.9],["plant",W/2-6,2,0,"fever",1.0],["plant",6,-2,0,"acacia",1.0],
         ["loot",0,H/2-20,0,"wilderness"],["loot",-5,H/2-15,0,"commercial"]],
        [for(k=[0:risk]) ["zed",-8+k*8,H/2-18-k*2,0,"civilian",1+risk]]);

function sv_settlement(k,W,H,n,risk,seed) =
    k=="sv_trading" ? sv_m_trading(W,H,n,risk,seed) : k=="sv_plots" ? sv_m_plots(W,H,n,risk,seed) :
    k=="sv_boma" ? sv_m_boma(W,H,n,risk,seed) : k=="sv_shamba" ? sv_m_shamba(W,H,n,risk,seed) :
    k=="sv_mission" ? sv_m_mission(W,H,n,risk,seed) : k=="sv_clinic" ? sv_m_clinic(W,H,n,risk,seed) :
    k=="sv_gate" ? sv_m_gate(W,H,n,risk,seed) : k=="sv_post" ? sv_m_gate(W,H,n,risk,seed,false) :
    sv_m_camp(W,H,n,risk,seed);

// ================= Furnished buildings =================
// Round hut: bed against the back wall, a low table, a hearth of three stones.
module nd_sv_hut(seed=0,r=3,p="residential")
{
    gk_flatten()
    {
        children();
        translate([0.3,r-1.35,0]) cw_prop_bed(seed);
        translate([-r+1.5,-0.2,0]) rotate([0,0,90]) cw_prop_table(seed+2,1.0,0.6);
        translate([r-1.5,-0.6,0]) cw_prop_chair(seed+3);
        color([0.10,0.10,0.09]) translate([0,-0.2,0.1]) for(a=[0:120:240]) rotate([0,0,a]) translate([0.22,0,0.07]) sphere(r=0.11,$fn=5);
    }
    translate([-r+1.5,-0.2,nd_table_top(seed+2)]) nd_loot(p=p);
    translate([0.6,r-1.35,0.46]) nd_loot(p=p);
    translate([r-1.4,0.5,0.1]) nd_loot(p=p);
}

// Maasai hut: a hide bed on a pole platform, calabashes by the hearth.
module nd_sv_manyatta(seed=0,L=6.5,D=5)
{
    gk_flatten()
    {
        children();
        color([0.25,0.19,0.13]) translate([L/2-1.75,D/2-1.2,0.1]) { translate([0,0,0.32]) cube([2.1,1.3,0.08],center=true); for(sx=[-1,1],sy=[-1,1]) translate([sx*0.95,sy*0.55,0]) cylinder(h=0.3,r=0.05,$fn=4); }
        color([0.33,0.27,0.20]) translate([L/2-1.75,D/2-1.2,0.47]) cube([1.9,1.1,0.03],center=true);
        color([0.10,0.10,0.09]) translate([-0.6,0.2,0.1]) for(a=[0:120:240]) rotate([0,0,a]) translate([0.22,0,0.07]) sphere(r=0.11,$fn=5);
        color([0.36,0.26,0.13]) for(i=[0:2]) translate([-L/2+0.9+i*0.4,D/2-0.8,0.3]) scale([1,1,1.5]) sphere(r=0.16,$fn=6);
    }
    translate([L/2-1.75,D/2-1.2,0.5]) nd_loot(p="farm");
    translate([-L/2+1.3,D/2-0.9,0.1]) nd_loot(p="farm");
    translate([-0.4,-0.9,0.1]) nd_loot(p="residential");
}

// Church / hall with the nave along y: bench rows, a lectern table at the back.
module nd_sv_hall(seed=0,L=9,D=16)
{
    gk_flatten()
    {
        children();
        for(y=[-D/2+4.2:2.2:D/2-5.0],sx=[-1,1]) translate([sx*L*0.24,y,0]) cw_prop_bench();
        translate([0,D/2-2.2,0]) cw_prop_table(seed+1,1.6,0.8);
        translate([L/2-1.2,D/2-1.6,0]) cw_prop_body_bag(seed);
        translate([-L/2+1.3,D/2-3.4,0]) cw_prop_body_bag(seed+3);
    }
    translate([0,D/2-2.2,nd_table_top(seed+1)]) nd_loot(p="residential");
    translate([-L/2+1.2,-D/2+1.6,0.1]) nd_loot(p="medical");
    translate([L/2-1.2,D/2-3.2,0.1]) nd_loot(p="residential");
}

// Long block of rooms: school (desks, cupboard) or clinic (beds, counter).
module nd_sv_block(seed=0,L=20,D=7,kind="school")
{
    nb=max(1,2*floor((L/5.5-1)/2)+1); bw=L/nb;
    gk_flatten()
    {
        children();
        for(i=[0:nb-1]) let(x=-L/2+(i+0.5)*bw)
        {
            if(kind=="clinic")
            {
                translate([x-bw*0.26,D/2-1.5,0]) rotate([0,0,90]) cw_prop_bed_hospital(seed+i*5);
                translate([x+bw*0.24,D/2-0.75,0]) cw_prop_locker(seed+i,2);
                if(i==floor(nb/2)) translate([x+bw*0.28,-0.9,0]) rotate([0,0,90]) cw_prop_counter(seed,1.8);
            }
            else
            {
                for(k=[0:1]) translate([x-bw*0.2+k*bw*0.3,D/2-1.6,0]) { cw_prop_table(seed+i*3+k,1.1,0.5); translate([0,-0.6,0]) cw_prop_bench(); }
                translate([x-bw*0.36,-0.4,0]) rotate([0,0,90]) cw_prop_shelf_wood(seed+i,1.4);
            }
        }
    }
    for(i=[0:nb-1]) let(x=-L/2+(i+0.5)*bw,p=kind=="clinic" ? "medical" : "residential")
    {
        translate([kind=="clinic" ? x+bw*0.24 : x-bw*0.2,D/2-(kind=="clinic" ? 1.4 : 1.6),kind=="clinic" ? 0.1 : nd_table_top(seed+i*3)]) nd_loot(p=p);
        translate([kind=="clinic" ? x-bw*0.26 : x-bw*0.36,kind=="clinic" ? D/2-1.5 : -0.4,kind=="clinic" ? 0.6 : 0.67]) nd_loot(p=p);
        translate([x+bw*0.1,-D/2+1.3,0.1]) nd_loot(p=kind=="clinic" ? "medical" : "commercial");
    }
}

// Safari tent: camp bed, trunk table, a canvas chair's worth of loot.
module nd_sv_tent(seed=0,L=5,D=7)
{
    gk_flatten()
    {
        children();
        translate([-L/2+1.05,D/2-1.3,0.1]) rotate([0,0,90]) cw_prop_bed(seed);
        translate([L/2-0.8,D/2-0.9,0.1]) cw_prop_table(seed+2,0.9,0.6);
        translate([L/2-0.9,-0.6,0.1]) cw_prop_chair(seed+3);
    }
    translate([-L/2+1.05,D/2-1.5,0.56]) nd_loot(p="residential");
    translate([L/2-0.8,D/2-0.9,0.1+nd_table_top(seed+2)]) nd_loot(p="commercial");
    translate([L/2-0.9,-1.6,0.1]) nd_loot(p="wilderness");
}

function sv_sign(kind,seed) = kind=="bar" ? ["BAR","HOTEL","HOTELI"][sv_rnd(seed,3)] : kind=="chemist" ? "CHEMIST" :
    kind=="butchery" ? "BUTCHERY" : ["DUKA","SHOP","GENERAL STORE","M-KIOSK",""][sv_rnd(seed+5,5)];

// Every building kind the savanna planner emits, plus the legacy encounter
// kinds, so no site of this region ever borrows another region's shell.
module nd_sv_building(kind="sv_house",seed=0,L=8,D=6,p="residential")
{
    if(kind=="duka" || kind=="butchery" || kind=="shop" || kind=="supermarket") nd_shop(seed,L,D,"commercial") sv_bldg_duka(seed,L,D,sv_sign(kind,seed));
    else if(kind=="chemist" || kind=="pharmacy") nd_shop(seed,L,D,"medical") sv_bldg_duka(seed,L,D,"CHEMIST");
    else if(kind=="bar" || kind=="pub") nd_pub(seed,L,D) sv_bldg_duka(seed,L,D,sv_sign("bar",seed));
    else if(kind=="store" || kind=="warehouse" || kind=="workshop" || kind=="garage" || kind=="barn") nd_warehouse(seed,L,D,kind=="barn" ? "farm" : "industrial") sv_bldg_store(seed,L,D);
    else if(kind=="armoury") nd_warehouse(seed,L,D,"military") sv_bldg_store(seed,L,D);
    else if(kind=="rondavel") nd_sv_hut(seed,min(L,D)/2,p) sv_bldg_rondavel(seed,L,D);
    else if(kind=="uniport" || kind=="barracks") nd_sv_hut(seed,min(L,D)/2,"military") sv_bldg_uniport(seed,L,D);
    else if(kind=="manyatta") nd_sv_manyatta(seed,L,D) sv_bldg_manyatta(seed,L,D);
    else if(kind=="church" || kind=="chapel") nd_sv_hall(seed,L,D) sv_bldg_church(seed,L,D);
    else if(kind=="school") nd_sv_block(seed,L,D,"school") sv_bldg_block(seed,L,D,"school");
    else if(kind=="clinic" || kind=="medical") nd_sv_block(seed,L,D,"clinic") sv_bldg_block(seed,L,D,"clinic");
    else if(kind=="ranger" || kind=="police" || kind=="hq" || kind=="office") nd_office(seed,L,D,kind=="police" || kind=="office" ? "police" : "military") sv_bldg_ranger(seed,L,D);
    else if(kind=="banda") nd_pub(seed,L,D) sv_bldg_banda(seed,L,D);
    else if(kind=="tent") nd_sv_tent(seed,L,D) sv_bldg_tent(seed,L,D);
    else nd_house_rural(seed,L,D,p) sv_bldg_house(seed,L,D);
}

function sv_is_prop(n) = n=="sv_stall" || n=="sv_boda" || n=="sv_sacks" || n=="sv_jerrycans" || n=="sv_pole" || n=="sv_tank" ||
    n=="sv_tank_tower" || n=="sv_latrine" || n=="sv_kraal" || n=="sv_thorn" || n=="sv_bones" || n=="sv_granary" || n=="sv_flagpole" ||
    n=="sv_gate_arch" || n=="sv_mast" || n=="sv_firepit" || n=="sv_termite" || n=="sv_rocks" || n=="sv_trough" || n=="ambulance" || n=="sandbag";
module nd_sv_prop(name="sv_stall",seed=0,a=0,b=0)
{
    if(name=="sv_stall") sv_prop_stall(seed);
    else if(name=="sv_boda") sv_prop_boda(seed);
    else if(name=="sv_sacks") sv_prop_sacks(seed);
    else if(name=="sv_jerrycans") sv_prop_jerrycans(seed);
    else if(name=="sv_pole") sv_prop_pole(seed);
    else if(name=="sv_tank") sv_prop_tank(seed);
    else if(name=="sv_tank_tower") sv_prop_tank_tower(seed);
    else if(name=="sv_latrine") sv_prop_latrine(seed);
    else if(name=="sv_kraal") sv_prop_kraal(seed,a,b);
    else if(name=="sv_thorn") sv_prop_thorn(seed,a);
    else if(name=="sv_bones") sv_prop_bones(seed);
    else if(name=="sv_granary") sv_prop_granary(seed);
    else if(name=="sv_flagpole") sv_prop_flagpole(seed);
    else if(name=="sv_gate_arch") sv_prop_gate_arch(seed,a);
    else if(name=="sv_mast") sv_prop_mast(seed);
    else if(name=="sv_firepit") sv_prop_firepit(seed);
    else if(name=="sv_termite") sv_prop_termite(seed);
    else if(name=="sv_rocks") sv_prop_rocks(seed);
    else if(name=="sv_trough") sv_prop_trough(seed);
    else if(name=="ambulance") cw_veh_ambulance(seed);
    else if(name=="sandbag") cw_prop_wall_sandbag(len=a);
}

// Savanna flora by species name (one mesh per species+seed).
module nd_sv_flora(sp="acacia",seed=0)
{
    if(sp=="acacia" || sp=="tree") sv_nature_acacia(seed,7+seed%4);
    else if(sp=="fever") sv_nature_fever(seed);
    else if(sp=="fig") sv_nature_fig(seed);
    else if(sp=="balanites") sv_nature_balanites(seed);
    else if(sp=="euphorbia") sv_nature_euphorbia(seed);
    else if(sp=="palm") sv_nature_palm(seed);
    else if(sp=="croton" || sp=="hedge") sv_nature_croton(seed);
    else if(sp=="thornbush" || sp=="shrub") sv_nature_thornbush(seed);
    else if(sp=="grass") sv_nature_grass(seed);
    else if(sp=="sisal") sv_nature_sisal(seed);
    else if(sp=="banana") sv_nature_banana(seed);
    else if(sp=="maize") sv_nature_maize(seed);
    else if(sp=="dead") sv_nature_dead(seed);
    else if(sp=="termite") sv_prop_termite(seed);
    else if(sp=="rocks") sv_prop_rocks(seed);
    else if(sp=="bones") sv_prop_bones(seed);
    else sv_nature_thornbush(seed);
}

// Land cover of the region; same tuple as nd_lc_recipe in nextdayz_pcg.scad:
// [paint, spacing, slopeMax, keep, edgeShrub, [[species,kind,weight,s0,s1],...]].
// The savanna is defined by how far apart its trees stand: open grassland with
// a lone tree every forty metres, acacia woodland, croton thicket on the
// luggas, and a closed gallery of figs and fever trees only where the river is.
function sv_lc_recipe(k) =
    k=="savanna" ? ["grass",19,30,0.5,0,[["grass","shrub",12,0.8,1.4],["acacia","tree",2,0.85,1.3],["balanites","tree",1,0.8,1.2],["thornbush","shrub",3,0.8,1.3],["termite","shrub",1,0.8,1.2],["dead","dead",1,0.8,1.1],["bones","shrub",0.4,1,1]]] :
    k=="acacia_woodland" ? ["grass",12.5,32,0.8,0,[["acacia","tree",8,0.85,1.35],["balanites","tree",1,0.8,1.2],["euphorbia","tree",1,0.8,1.2],["thornbush","shrub",4,0.8,1.3],["grass","shrub",5,0.8,1.4],["dead","dead",1,0.8,1.1],["rocks","shrub",0.6,0.7,1.2]]] :
    k=="gallery" ? ["grass_dark",8.2,38,0.95,0.3,[["fig","tree",3,0.8,1.2],["fever","tree",5,0.8,1.2],["palm","tree",2,0.8,1.15],["acacia","tree",1,0.9,1.2],["croton","shrub",3,0.8,1.2]]] :
    k=="thicket" ? ["dry_grass",7.5,34,0.78,0,[["croton","shrub",8,0.8,1.4],["thornbush","shrub",3,0.9,1.4],["euphorbia","tree",1,0.8,1.2],["acacia","tree",1,0.7,1.0],["rocks","shrub",0.5,0.7,1.1]]] :
    k=="shamba" ? ["dry_grass",5.5,20,0.6,0,[["maize","shrub",12,0.85,1.2],["sisal","shrub",0.6,0.9,1.2],["banana","tree",0.4,0.9,1.1]]] :
    k=="grazing" ? ["dry_grass",26,28,0.5,0,[["thornbush","shrub",4,0.7,1.1],["acacia","tree",1,0.8,1.1],["bones","shrub",1,1,1],["termite","shrub",1,0.7,1.0],["dead","dead",1,0.7,1.0]]] :
    k=="banana" ? ["grass_dark",5.5,22,0.85,0,[["banana","tree",1,0.9,1.25]]] :
    ["",0,0,0,0,[]];
