// nextdayz_settlement.scad — settlement morphology for geographic NextDayz maps.
// Pure functions: a site [id,name,kind,center,size,rotation,buildings,risk]
// expands into site-local items, so the TERR pads and the geometry are derived
// from one plan. Hot reload re-evaluates it like any other SCAD.
//
// Items (site-local metres, x along the site's width, y along its depth):
//   ["pad",x,y,rot,w,d,surface,apron]   flat plot; surface = TERR biome name
//   ["road",[[x,y],...],width]          lane / street (TERR road op)
//   ["lake",x,y,rot,r,depth]            village pond (TERR lake op)
//   ["paint",[[x,y],...],biome]         land cover inside the settlement
//   ["bldg",x,y,rot,kind,L,D,opt]       building (front = -y after rot)
//   ["prop",x,y,rot,name,a,b]           settlement furniture
//   ["plant",x,y,rot,species,scale]     garden / green trees
//   ["loot",x,y,rot,profile] / ["zed",x,y,rot,profile,group]
// Every morph emits exactly `buildings` bldg items. Real places follow these
// shapes: buildings line streets and greens; plots are terraced individually;
// yards are earth, squares are paved, gardens stay green.
// Pads that may sit at different heights keep >= nd_gap() metres apart: each
// plot is flat up to its edge on the 3.9 m terrain mesh only if the neighbour's
// flat shoulder (one cell diagonal) cannot reach it. Gardens, lanes and greens
// fill those gaps, which is also how real plots are spaced.
use <kit_bohemia.scad>
use <nextdayz_hokkaido.scad>

function nd_u(s,k) = bh_rnd(s*7+k*131,1000)/999;
function nd_gap() = 12;
function nd_sum(v,i=0) = i>=len(v) ? 0 : v[i]+nd_sum(v,i+1);
function nd_cum(v,i) = i<=0 ? 0 : v[i-1]+nd_cum(v,i-1);
function nd_rot2(p,a) = [p[0]*cos(a)-p[1]*sin(a),p[0]*sin(a)+p[1]*cos(a)];
function nd_xf(p,o,a) = [o[0],o[1]]+nd_rot2(p,a);
function nd_tail(it,i) = i>=len(it) ? [] : [for(k=[i:len(it)-1]) it[k]];
function nd_tf(it,o,a) = (it[0]=="road" || it[0]=="paint") ? concat([it[0],[for(p=it[1]) nd_xf(p,o,a)]],nd_tail(it,2)) :
    let(q=nd_xf([it[1],it[2]],o,a)) concat([it[0],q[0],q[1],it[3]+a],nd_tail(it,4));
function nd_tfs(items,o,a) = [for(it=items) nd_tf(it,o,a)];
function nd_far(p,list,r,i=0) = i>=len(list) ? true : norm([p[0]-list[i][0],p[1]-list[i][1]])>r && nd_far(p,list,r,i+1);
function nd_count_bldg(items) = len([for(it=items) if(it[0]=="bldg") 1]);

function nd_is_settlement(kind) = kind=="naves" || kind=="homestead" || kind=="square" || kind=="street" ||
    kind=="suburb" || kind=="chaty" || kind=="jzd" || kind=="villas" || kind=="factory" || kind=="hillcastle" || hk_is_settlement(kind);

// Row of m buildings across length Lr: [[centre offset, width], ...]; widths vary 7..12 m.
function nd_row(m,Lr,seed,wmin=7,wmax=12) = m<=0 ? [] : let(
    base=Lr/m,
    w0=[for(i=[0:m-1]) min(wmax,max(wmin,base*(0.84+0.32*nd_u(seed,i))))],
    T=nd_sum(w0), k=T>Lr ? Lr/T : 1,
    w=[for(x=w0) x*k], gap=T>Lr || m<2 ? 0 : (Lr-T)/(m-1))
    [for(i=[0:m-1]) [-Lr/2+nd_cum(w,i)+i*gap+w[i]/2,w[i]]];

// Town frontage: burgher fronts with a few shops, a pharmacy and a pub.
function nd_town_kind(i,m,seed,row) = let(u=nd_u(seed,i*3+row))
    row==0 && i==floor(m/2)-1 ? "pharmacy" : row==1 && i==m-1 ? "pub" : u<0.22 ? "shop" : "burgher";
function nd_town_opt(i,seed) = [nd_u(seed,i*5+1)<0.38 ? 3 : 2,nd_u(seed,i*5+2)<0.4 ? 1 : 0];

// --- Farmstead in plot space: u along the street, v outward (v=0 frontage). ---
// House gable to the street with its door on the yard, barn closing the back,
// optional side wing; walled frontage with an arched cart gate; orchard behind.
function nd_farmstead(pw,pd,nb,seed) = let(
    hL=11+nd_u(seed,1)*2.5, hD=7.6+nd_u(seed,2)*0.8,
    bL=min(18,pw-2), bD=8.5+nd_u(seed,3)*0.8,
    hu=pw/2-hD/2-1.0, hv=2.2+hL/2,
    sL=min(10,pd-bD-6), sD=6.4,
    su=-pw/2+sD/2+1.0, sv=2.6+sL/2,
    wl=nb>=3 ? -pw/2+sD+1.5 : -pw/2+0.4, wr=hu-hD/2-0.35,
    eg=nd_u(seed,4)<0.3)
    concat(
        [["pad",0,pd/2,0,pw,pd,"road",3]],
        [["bldg",hu,hv,-90,eg ? "egerland" : "farmhouse",hL,hD,[1]]],
        [["bldg",0,pd-bD/2-1,0,"barn",bL,bD,[]]],
        nb>=3 ? [["bldg",su,sv,90,"stable",sL,sD,[]]] : [],
        wr-wl>6.8 ? [["prop",(wl+wr)/2,0.55,0,"yard_wall",wr-wl,3.4]] : [],
        [["loot",(wl+wr)/2+1,pd*0.42,0,"farm"]],
        [["prop",hu-hD/2-0.9,hv+hL*0.3,90,"woodpile",0,0]],
        nd_u(seed,6)<0.5 ? [["prop",-pw*0.2,pd*0.55,nd_u(seed,7)*90,"car",0,0]] : [],
        [for(k=[0:3]) ["plant",-pw/2+3+k*(pw-6)/3+(nd_u(seed,10+k)-0.5)*2,pd+3.5+nd_u(seed,20+k)*7,nd_u(seed,30+k)*360,"fruit",0.85+nd_u(seed,40+k)*0.45]],
        nd_u(seed,5)<0.6 ? [["prop",-pw/2+2.5,pd+2,0,"bale",0,0],["prop",-pw/2+4.4,pd+2.6,0,"bale",0,0]] : []);

// --- Návesní ves: farmsteads around an elongated green with chapel and pond. ---
// Row plot = [kind,width]: "farm" (2-3 buildings), "fire", "pub"; plots keep
// garden gaps of nd_gap() between them, the green keeps the chapel clear.
function nd_naves_rows(n) = let(
    spec=n>=13 ? 3 : n>=9 ? 2 : n>=5 ? 1 : 0,
    F=n-spec, nf=max(1,floor(F/2)), extra=F-2*nf,
    top=ceil(nf/2), bot=nf-top)
    [concat(spec>=3 ? [["pub",16]] : [],[for(i=[0:top-1]) ["farm",24,2+(i<extra ? 1 : 0)]]),
     concat(spec>=2 ? [["fire",16]] : [],bot>0 ? [for(i=[0:bot-1]) ["farm",24,2]] : [])];
function nd_row_len(row) = nd_sum([for(r=row) r[1]])+nd_gap()*(len(row)-1);
function nd_naves_plot(r,pd,seed) =
    r[0]=="farm" ? nd_farmstead(r[1],pd,r[2],seed) :
    r[0]=="fire" ? [["pad",0,pd/2-4,0,r[1],pd-8,"pad",2.5],["bldg",0,9,0,"firestation",11,8,[]],["prop",4,3,0,"car",0,0]] :
    [["pad",0,pd/2-4,0,r[1],pd-8,"road",2.5],["bldg",0,8.5,0,"pub",12,9,[]],["prop",-5.5,3,0,"bench",0,0]];
function nd_m_naves(W,H,n,risk,seed) = let(
    rows=nd_naves_rows(n), spec=n>=13 ? 3 : n>=9 ? 2 : n>=5 ? 1 : 0,
    g=max(38,min(44,H*0.34)),
    pd=min(27,H/2-g/2-9),
    lane=g/2-3)
    concat(
        [["road",[[-W/2+1,-lane],[W/2-1,-lane]],5.5],["road",[[-W/2+7,lane],[W/2-7,lane]],4.5]],
        [["paint",[[-W/2+6,-lane+4],[W/2-6,-lane+4],[W/2-6,lane-4],[-W/2+6,lane-4]],"grass"]],
        [for(sd=[0,1]) let(row=rows[sd],L=nd_row_len(row),x0=-L/2) if(len(row)>0) for(k=[0:len(row)-1])
            let(xc=x0+nd_sum([for(q=[0:k]) row[q][1]])-row[k][1]/2+k*nd_gap())
            each nd_tfs(nd_naves_plot(row[k],pd,seed+sd*501+k*97),sd==0 ? [xc,g/2] : [-xc,-g/2],sd==0 ? 0 : 180)],
        spec>=1 ? [["pad",0,0,0,11,13,"grass",2.5],["bldg",0,0,0,"chapel",6,8,[]],
                   ["plant",-6,3,0,"linden",1.0],["plant",6,3,90,"linden",0.95],["loot",0,-5,0,"medical"]] : [],
        [["lake",-W*0.26,0,0,min(8,lane-6),1.6],["plant",-W*0.26-10,4,0,"willow",1],["plant",-W*0.26+10,-3,0,"willow",0.9],
         ["prop",-W*0.26+11,5,0,"pump",0,0]],
        [["prop",W*0.2,1,0,"memorial",0,0],["prop",-W/2+4,-lane+7,0,"cross",0,0],
         ["prop",-W*0.1,-lane+7.5,0,"bus_shelter",0,0],["prop",-W*0.04,lane-6.5,0,"pole",0,0],
         ["prop",W*0.34,-lane+6.5,0,"pole",0,0],["prop",W*0.08,-lane+7,0,"car",0,0],
         ["plant",W*0.2+5,4,0,"linden",0.9]],
        [for(k=[0:risk+floor(n/4)]) ["zed",(k%2==0 ? -1 : 1)*W*(0.1+0.09*k),(k%2==0 ? -lane : lane),0,"civilian",1+risk]]);

// --- Samota: a single closed courtyard farm in its fields. ---
function nd_m_homestead(W,H,n,risk,seed) = let(
    pw=min(32,W-8), pd=min(30,H-26), nb=min(3,max(2,n)),
    gx=-pw*0.05)
    concat(
        nd_tfs(nd_farmstead(pw,pd,nb,seed),[0,-pd/2],0),
        [["road",[[gx,-pd/2-1],[gx,-H/2+1]],3.5]],
        [["plant",gx+6,-pd/2-5,0,"linden",1.1],["prop",gx+4,-pd/2-4,0,"cross",0,0],["prop",pw*0.15,pd*0.05,0,"pump",0,0]],
        [for(i=[0:2],j=[0:1]) ["plant",-pw/2+4+i*(pw-8)/2+(nd_u(seed,i*3+j)-0.5)*2,pd/2+6+j*7,i*40,"fruit",0.9+nd_u(seed,9+i)*0.3]],
        [["zed",0,0,0,"civilian",1+risk],["zed",pw*0.3,-pd/2-6,0,"civilian",1]]);

// --- Náměstí: paved square, continuous burgher frontage, church and column. ---
function nd_m_square(W,H,n,risk,seed) = let(
    Db=12, church=n>=12 ? 1 : 0, hall=n>=8 ? 1 : 0,
    R=n-church-hall,
    nt=ceil(R*0.38), nb=ceil((R-nt)*0.62), nl=R-nt-nb,
    ys=H/2-3-Db/2,
    xa=-W/2+3+(nl>0 ? Db+9 : 4), xb=W/2-3-(church ? 27 : 4),
    Lr=xb-xa, xm=(xa+xb)/2,
    tops=nd_row(nt,Lr-(hall ? 17 : 0),seed),
    bL=(Lr-8)/2,
    bots1=nd_row(ceil(nb/2),bL,seed+11), bots2=nd_row(nb-ceil(nb/2),bL,seed+23),
    Ll=2*(ys-Db/2)-16, lefts=nd_row(nl,Ll,seed+37),
    sq0=xa+2, sq1=xb-2, cx=(sq0+sq1)/2)
    concat(
        [["pad",0,0,0,W-2,H-2,"pad",4]],
        [for(i=[0:len(tops)-1]) let(x=xa+(hall ? 0 : 0)+tops[i][0]+Lr/2-(hall ? 17 : 0)/2-(Lr)/2+(Lr-(hall ? 17 : 0))/2,
                                     xx=(hall && x>xm-8.5) ? x+17 : x)
            ["bldg",xa+(Lr-(hall ? 17 : 0))/2+tops[i][0]+(hall && tops[i][0]+(Lr-17)/2>Lr/2-8.5 ? 17 : 0),ys,0,
             nd_town_kind(i,len(tops),seed,0),tops[i][1],Db,nd_town_opt(i,seed)]],
        hall ? [["bldg",xm,ys-0.5,0,"townhall",16,Db+1,[]]] : [],
        [for(i=[0:len(bots1)-1]) ["bldg",xa+bL/2+bots1[i][0],-ys,180,nd_town_kind(i,len(bots1),seed+5,1),bots1[i][1],Db,nd_town_opt(i,seed+5)]],
        [for(i=[0:len(bots2)-1]) ["bldg",xb-bL/2+bots2[i][0],-ys,180,nd_town_kind(i,len(bots2),seed+9,2),bots2[i][1],Db,nd_town_opt(i,seed+9)]],
        nl>0 ? [for(i=[0:len(lefts)-1]) ["bldg",-W/2+3+Db/2,lefts[i][0],90,nd_town_kind(i,len(lefts),seed+13,2),lefts[i][1],Db,nd_town_opt(i,seed+13)]] : [],
        church ? [["bldg",W/2-3-6.5,0,-90,"church",24,11,[]]] : [],
        [["prop",cx,0,0,"plague_column",0,0],["prop",cx-10,-9,0,"pump",0,0],["prop",cx+min(18,(sq1-sq0)*0.28),-4,0,"fountain",0,0],
         ["prop",cx-min(20,(sq1-sq0)*0.3),5,30,"car",0,0],["prop",cx+10,9,-20,"car",0,0],
         ["prop",sq0+3,ys-Db/2-3.5,0,"lamp",0,0],["prop",sq1-3,-ys+Db/2+3.5,0,"lamp",0,0],
         ["prop",cx-6,-ys+Db/2+4,0,"bench",0,0],["prop",cx+6,ys-Db/2-4,180,"bench",0,0]],
        [["plant",sq0+4,ys-Db/2-6,0,"linden",0.9],["plant",sq0+4,-ys+Db/2+6,0,"linden",0.85],
         ["plant",sq1-4,ys-Db/2-6,0,"linden",0.95]],
        [["loot",cx,-3,0,"commercial"],["loot",cx+min(18,(sq1-sq0)*0.28),-1,0,"residential"]],
        [for(k=[0:risk+1+floor(n/8)]) ["zed",cx+(k%2==0 ? -1 : 1)*(6+k*6),(k%3-1)*8,0,"civilian",1+risk]]);

// --- Town street: terraced sections with continuous frontage on both sides. ---
function nd_m_street(W,H,n,risk,seed) = let(
    Db=11, S=max(1,round(W/60)), gap=nd_gap(), secL=(W-2-(S-1)*gap)/S,
    rows=2*S, per=[for(r=[0:rows-1]) floor(n/rows)+(r<n%rows ? 1 : 0)],
    yf=5+Db/2)
    concat(
        [for(s=[0:S-1]) ["pad",-W/2+1+secL/2+s*(secL+gap),0,0,secL,H-2,"pad",3.5]],
        [["road",[[-W/2+1,0],[W/2-1,0]],7]],
        [for(r=[0:rows-1]) let(s=floor(r/2),sd=r%2==0 ? 1 : -1,row=nd_row(per[r],secL-2,seed+r*29),
                                 x0=-W/2+1+secL/2+s*(secL+gap))
            if(per[r]>0) for(i=[0:len(row)-1])
                ["bldg",x0+sd*row[i][0],sd*yf,sd>0 ? 0 : 180,nd_town_kind(i,len(row),seed+r,r%2),row[i][1],Db,[2,nd_u(seed,r*13+i)<0.3 ? 1 : 0]]],
        [for(s=[0:S-1]) let(x0=-W/2+1+secL/2+s*(secL+gap)) each
            [["prop",x0-secL*0.3,3.9,4,"car",0,0],["prop",x0+secL*0.25,-4.1,180,"car",0,0],["prop",x0,4.6,0,"lamp",0,0],
             ["zed",x0+secL*0.1,0,0,"civilian",1+risk]]],
        [for(s=[0:S-2]) let(xg=-W/2+1+(s+1)*secL+s*gap+gap/2) ["plant",xg,H/2-6,0,"linden",0.8]],
        [["loot",0,2.5,0,"commercial"]]);

// --- Suburb of detached family houses on garden plots along a road. ---
function nd_suburb_house(i,seed,villas) = let(u=nd_u(seed,i*7))
    villas ? (u<0.8 ? "villa" : "kostka") : (u<0.6 ? "kostka" : u<0.85 ? "farmhouse" : "villa");
// Plots share a terrace in groups of up to three; terraces keep nd_gap().
function nd_groups(m,k=3) = [for(t=[0:ceil(m/k)-1]) min(k,m-t*k)];
function nd_m_suburb(W,H,n,risk,seed,villas=false) = let(
    top=ceil(n/2), bot=n-top, pw=villas ? 21 : 16,
    pd=min(villas ? 28 : 24,H/2-5), yf=4.5)
    concat(
        [["road",[[-W/2+1,0],[W/2-1,0]],villas ? 5 : 6]],
        [for(r=[0,1]) let(m=r==0 ? top : bot,sd=r==0 ? 1 : -1,gs=m>0 ? nd_groups(m) : [],
                           L=m*pw+nd_gap()*(len(gs)-1))
            if(m>0) for(t=[0:len(gs)-1]) let(tx=-L/2+(nd_sum([for(q=[0:t]) gs[q]])-gs[t])*pw+t*nd_gap()+gs[t]*pw/2)
                each nd_tfs(concat(
                    [["pad",0,pd/2,0,gs[t]*pw,pd,"grass",2.5]],
                    [for(k=[0:gs[t]-1]) let(i=nd_sum([for(q=[0:t]) gs[q]])-gs[t]+k, s=seed+r*331+i*53,
                                             kind=nd_suburb_house(r*17+i,seed,villas),
                                             L0=kind=="villa" ? 12 : kind=="farmhouse" ? 11.5 : 9,
                                             D0=kind=="villa" ? 10 : kind=="farmhouse" ? 8 : 9,
                                             ux=-gs[t]*pw/2+(k+0.5)*pw)
                        each [["bldg",ux+(nd_u(s,1)-0.5)*max(0,pw-3-L0),5+D0/2+nd_u(s,2)*2,0,kind,L0,D0,kind=="farmhouse" ? [1] : []],
                              ["prop",ux-pw/4-0.6,0.3,0,"fence",pw/2-3.4,0],["prop",ux+pw/4+0.6,0.3,0,"fence",pw/2-3.4,0],
                              ["plant",ux-pw/2+2.5,pd-3.5,0,nd_u(s,3)<0.5 ? "spruce_garden" : "fruit",0.9],
                              ["loot",ux,pd*0.78,0,"residential"]]],
                    [for(k=[0:gs[t]-1]) let(s=seed+r*331+t*97+k) if(nd_u(s,5)<0.35) ["prop",-gs[t]*pw/2+(k+0.5)*pw+pw/2-3,5.5,90,"car",0,0]],
                    [["plant",-gs[t]*pw/2-nd_gap()/2,pd*0.6,0,villas ? "linden" : "fruit",1.0]]),[tx,sd*yf],sd>0 ? 0 : 180)],
        [for(k=[0:risk+floor(n/5)]) ["zed",-W/2+12+k*(W-24)/max(1,risk+floor(n/5)),(k%2==0 ? -2 : 2),0,"civilian",1+risk]]);

// --- Chatová osada: weekend cottages among forest trees on a slope. ---
function nd_m_chaty(W,H,n,risk,seed) = let(
    cols=max(1,ceil(sqrt(n*W/H))), rows=ceil(n/cols), cw=W/cols, ch=H/rows,
    cot=[for(i=[0:n-1]) let(c=i%cols,r=floor(i/cols))
        [-W/2+(c+0.5)*cw+(nd_u(seed,i)-0.5)*max(0,cw-11-nd_gap()),-H/2+(r+0.5)*ch+(nd_u(seed,i+50)-0.5)*max(0,ch-11-nd_gap()),
         floor(nd_u(seed,i+90)*4)*90+(nd_u(seed,i+120)-0.5)*24]],
    trees=[for(gx=[-W/2+4:7:W/2-4],gy=[-H/2+4:7:H/2-4]) let(k=round(gx*31+gy*17),p=[gx+(nd_u(seed,k)-0.5)*5,gy+(nd_u(seed,k+1)-0.5)*5])
        if(nd_far(p,cot,9.5) && (rows<2 || abs(p[1])>4)) [p[0],p[1],nd_u(seed,k+2)]])
    concat(
        rows>=2 ? [["road",[[-W/2+1,0],[W/2-1,0]],3.5]] : [],
        [for(i=[0:n-1]) ["pad",cot[i][0],cot[i][1],cot[i][2],11,11,"grass_dark",2.5]],
        [for(i=[0:n-1]) ["bldg",cot[i][0],cot[i][1],cot[i][2],"chata",5.5+nd_u(seed,i+200)*1.5,4.6+nd_u(seed,i+230)*0.8,[]]],
        [for(t=trees) ["plant",t[0],t[1],t[2]*360,t[2]<0.55 ? "spruce" : t[2]<0.8 ? "birch" : "pine",0.85+t[2]*0.35]],
        [for(i=[0:n-1]) if(i%3==0) ["prop",cot[i][0]+4.5,cot[i][1]-4.5,0,"woodpile",0,0]],
        [["prop",cot[0][0]+3,cot[0][1]+5,0,"pump",0,0]],
        [for(k=[0:risk]) ["zed",cot[k%n][0]+3,cot[k%n][1]+6,0,"civilian",risk]],
        [["loot",cot[0][0]-4,cot[0][1]-5,0,"wilderness"]]);

// --- JZD collective farm: cowsheds, silos, machinery yard on concrete. ---
function nd_m_jzd(W,H,n,risk,seed) = let(
    k=n>=5 ? 3 : 2, hall=n>=4 ? 1 : 0, barn=n>=6 ? 1 : 0, office=n-k-hall-barn,
    L=min(40,W-40))
    concat(
        [["pad",0,0,0,W-2,H-2,"pad",4]],
        [for(j=[0:k-1]) ["bldg",-W/2+4+L/2,H/2-11-j*23,0,"cowshed",L,11,[]]],
        hall ? [["bldg",W/2-15,H/2-12,0,"factory",20,12,[]]] : [],
        barn ? [["bldg",W/2-14,-H/2+26,0,"barn",18,9,[]]] : [],
        office>0 ? [for(o=[0:office-1]) ["bldg",W/2-10-o*14,-H/2+10,0,"kostka",9,9,[]]] : [],
        [["prop",W/2-12,H/2-34,0,"silo",0,0],["prop",-W/2+12,-H/2+8,0,"tractor",0,0],["prop",-W/2+20,-H/2+6,0,"fueltank",0,0],
         ["prop",W*0.05,-H/2+12,20,"truck",0,0],["prop",-W/2+30,-H/2+10,0,"bale",0,0],["prop",-W/2+31.5,-H/2+11.2,0,"bale",0,0],
         ["prop",-W/4,-H/2+2,0,"fence_concrete",W/2-10,0],["prop",W/4+4,-H/2+2,0,"fence_concrete",W/2-18,0]],
        [["loot",0,-H/2+16,0,"farm"],["loot",-W/2+14,-H/2+12,0,"industrial"],["prop",-W/2+38,-H/2+14,0,"pump",0,0]],
        [for(z=[0:risk+1]) ["zed",-W/4+z*W/5,H/2-22-(z%2)*23,0,"civilian",1+risk]]);

// --- Factory compound (porcelain works): brick halls, chimney, office. ---
function nd_m_factory(W,H,n,risk,seed) = let(
    L1=min(34,W-34))
    concat(
        [["pad",0,0,0,W-2,H-2,"pad",4]],
        [["bldg",-W/2+4+L1/2,H/2-12,0,"factory",L1,16,[]]],
        n>=2 ? [["bldg",W/2-13,-H/2+11,0,"burgher",14,11,[3,0]]] : [],
        n>=3 ? [["bldg",-W/2+4+11,-H/2+11,0,"factory",22,12,[]]] : [],
        n>=4 ? [["bldg",W/2-14,H/2-12,0,"cowshed",20,10,[]]] : [],
        n>=5 ? [["bldg",0,-H/2+9,0,"stable",11,7,[]]] : [],
        [["prop",W/2-6,0,0,"chimney",0,0],["prop",0,0,30,"truck",0,0],["prop",-8,-4,0,"fueltank",0,0]],
        [["loot",4,2,0,"industrial"],["loot",-W/4,-2,0,"industrial"]],
        [for(z=[0:risk+1]) ["zed",-W/3+z*W/4,(z%2-0.5)*10,0,"industrial",1+risk]]);

// --- Hill castle with burgher houses along the approach lane. ---
function nd_m_hillcastle(W,H,n,risk,seed) = let(cL=min(34,W-4), cD=26, cy=H/2-1-(cD+2)/2, m=n-1,
    a1=cy-(cD+2)/2-nd_gap(), a0=-H/2+1, ly=(a0+a1)/2)
    concat(
        [["pad",0,cy,0,cL+2,cD+2,"pad",3]],
        [["bldg",0,cy,0,"castle",cL,cD,[]]],
        m>0 ? [["pad",0,ly,0,W-4,a1-a0,"pad",3],["road",[[0,a1],[0,cy-cD/2]],5]] : [],
        m>0 ? [for(i=[0:m-1]) let(sd=i%2==0 ? -1 : 1) ["bldg",sd*9,ly-(floor(i/2)-(ceil(m/2)-1)/2)*11,sd<0 ? 90 : -90,"burgher",10,11,[2,0]]] : [],
        [["prop",0,ly,0,"car",0,0],["loot",0,cy,0,"military"]],
        [for(z=[0:risk]) ["zed",(z%2-0.5)*8,cy-4+z*2,0,"military",1+risk]]);

function nd_settlement(site,seed) = let(k=site[2],W=site[4][0],H=site[4][1],n=site[6],r=site[7])
    hk_is_settlement(k) ? hk_settlement(k,W,H,n,r,seed) :
    k=="naves" ? nd_m_naves(W,H,n,r,seed) : k=="homestead" ? nd_m_homestead(W,H,n,r,seed) :
    k=="square" ? nd_m_square(W,H,n,r,seed) : k=="street" ? nd_m_street(W,H,n,r,seed) :
    k=="suburb" ? nd_m_suburb(W,H,n,r,seed) : k=="villas" ? nd_m_suburb(W,H,n,r,seed,true) :
    k=="chaty" ? nd_m_chaty(W,H,n,r,seed) : k=="jzd" ? nd_m_jzd(W,H,n,r,seed) :
    k=="factory" ? nd_m_factory(W,H,n,r,seed) : k=="hillcastle" ? nd_m_hillcastle(W,H,n,r,seed) : [];

// TERR operators of every settlement site, in world space. Order inside a site:
// roads, ponds, pads (plots last so they own their floors), then paints.
function nd_settlement_terrain(sites,seed) = [for(i=[0:len(sites)-1]) let(s=sites[i]) if(nd_is_settlement(s[2]))
    let(items=nd_settlement(s,seed+i*131),o=s[3],a=s[5]) each concat(
        [for(it=items) if(it[0]=="road") ["road",[for(p=it[1]) nd_xf(p,o,a)],it[2]]],
        [for(it=items) if(it[0]=="lake") ["lake",nd_xf([it[1],it[2]],o,a),it[4],it[5]]],
        [for(it=items) if(it[0]=="pad") ["pad",nd_xf([it[1],it[2]],o,a),[it[4],it[5]],it[3]+a,it[7],it[6]]],
        [for(it=items) if(it[0]=="paint") ["paint",[for(p=it[1]) nd_xf(p,o,a)],it[2]]])];
