// Regional settlement plans. Same item protocol as nextdayz_settlement.scad.
// Each plot owns a pad; neighbouring terraces keep a 12m garden / alley gap.
function hk_is_settlement(k) = k=="hokkaido_lane" || k=="shotengai" || k=="canal" || k=="portyard" || k=="shrine";
function hk_m_lane(W,H,n,risk,seed,shops=false) = let(
    rows=2, m=ceil(n/2), groups=ceil(m/2), gap=12,
    seg=(W-8-(groups-1)*gap)/groups, depth=shops ? 16 : 24,
    y0=shops ? 6 : 5)
    concat(
        [["road",[[-W/2+1,0],[W/2-1,0]],shops ? 8 : 6]],
        [for(r=[0:1],g=[0:groups-1]) let(sd=r==0 ? 1 : -1, cnt=min(2,(r==0 ? m : n-m)-g*2),xc=-W/2+4+seg/2+g*(seg+gap)) if(cnt>0)
            each concat(
                [["pad",xc,sd*(y0+depth/2),0,seg,depth,shops ? "pad" : "grass",2.5]],
                [for(i=[0:cnt-1]) let(k=r*m+g*2+i,x=xc+(i-(cnt-1)/2)*(seg/cnt),L=min(shops ? 11 : 10,seg/cnt-1.4),D=shops ? 11 : 9,
                    kind=shops ? (k%7==0 ? "pharmacy" : k%5==0 ? "pub" : "shop") :
                        (k%9==0 ? "apartment" : k%5==0 ? "townhouse" : k%7==0 ? "ruin" : "cottage"))
                    ["bldg",x,sd*(y0+2+D/2),sd>0 ? 0 : 180,kind,L,D,[]]],
                [["prop",xc-seg*0.3,sd*(y0+0.5),0,"hk_pole",0,0]],
                shops ? [["prop",xc+seg*0.3,sd*(y0+0.5),sd>0 ? 0 : 180,"hk_vending",0,0]] :
                    [["plant",xc-seg*0.28,sd*(y0+depth-3),0,"elm",0.65],["loot",xc,sd*(y0+depth-2),0,"residential"]])],
        [for(k=[0:risk+1]) ["zed",-W*0.3+k*W*0.6/(risk+1),(k%2-0.5)*3,0,"civilian",risk+1]],
        [["prop",W*0.28,-3.8,4,"car",0,0]]);

// One-sided stone-store frontage with a continuous loading lane. Rotating the
// site aligns the warehouses with the north-south canal instead of a grid camp.
function hk_m_canal(W,H,n,risk,seed) = let(pitch=(W-8)/n,pw=pitch-12,D=15,L=min(25,pw-2))
    concat(
        [["road",[[-W/2+1,-9],[W/2-1,-9]],7]],
        [for(i=[0:n-1]) let(x=-W/2+4+(i+0.5)*pitch) each
            [["pad",x,3,0,pw,34,"pad",2.5],["bldg",x,6,0,i%4==3 ? "workshop" : "warehouse",L,D,[]],
             ["prop",x+pw*0.32,-8,0,"hk_bollard",0,0],["loot",x+L*0.3,-4,0,"industrial"]]],
        [for(i=[0:risk+1]) ["zed",-W*0.35+i*W*0.7/(risk+1),-9,0,"industrial",risk+1]]);

function hk_m_portyard(W,H,n,risk,seed) = let(m=ceil(n/2),pitch=(W-10)/m,pw=pitch-12,D=16,L=min(27,pw-2))
    concat(
        [["road",[[-W/2+1,0],[W/2-1,0]],9]],
        [for(i=[0:n-1]) let(r=floor(i/m),sd=r==0 ? 1 : -1,x=-W/2+5+(i%m+0.5)*pitch,y=sd*(H/2-19)) each
            [["pad",x,y,0,pw,32,"pad",2.5],["bldg",x,y,sd>0 ? 0 : 180,i==0 ? "office" : i%3==0 ? "workshop" : "warehouse",L,D,[]],
             ["prop",x+pw*0.32,y-sd*12,0,"hk_bollard",0,0],["loot",x-L*0.3,y-sd*11,0,"industrial"]]],
        [["prop",-W*0.25,-3.8,0,"truck",0,0],["loot",0,2.8,0,"industrial"]],
        [for(k=[0:risk+1]) ["zed",-W*0.3+k*W*0.6/(risk+1),0,0,"industrial",risk+1]]);

function hk_m_shrine(W,H,n,risk,seed) = concat(
    [["pad",0,12,0,22,22,"grass",3],["bldg",0,12,0,"shrine",11,9,[]],
     ["road",[[0,-H/2+1],[0,6]],3.5],["prop",0,-H*0.3,0,"hk_torii",5,0],
     ["prop",-4,-1,0,"hk_lantern",0,0],["prop",4,-1,0,"hk_lantern",0,0],
     ["plant",-W/2+5,20,0,"elm",1],["plant",W/2-5,18,0,"birch",1],
     ["loot",0,8,0,"wilderness"],["zed",0,-10,0,"civilian",risk]],
    n>=2 ? [["pad",-W/2+13,-21,0,18,16,"grass",2.5],["bldg",-W/2+13,-21,0,"cottage",9,7,[]]] : []);

function hk_settlement(k,W,H,n,risk,seed) =
    k=="hokkaido_lane" ? hk_m_lane(W,H,n,risk,seed) : k=="shotengai" ? hk_m_lane(W,H,n,risk,seed,true) :
    k=="canal" ? hk_m_canal(W,H,n,risk,seed) : k=="portyard" ? hk_m_portyard(W,H,n,risk,seed) : hk_m_shrine(W,H,n,risk,seed);
