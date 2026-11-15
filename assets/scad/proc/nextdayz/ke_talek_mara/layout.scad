// Gameplay-authored PCG controls over a geographic reference. Metres; +y north.
// Site = [id,name,kind,center,size,rotation,buildingCount,risk(1..3)].
// Counts and routes are level-design decisions, never OSM footprint counts.
// Keep literal arrays here so the exporter can validate budgets and safe sites.
//
// Talek v1: the Talek River loops north-west across the tile and is the edge of
// the Maasai Mara reserve. East of it lies community land: the trading centre
// on its murram street, the mission school, the health centre and the police
// post, plots and shambas around them, three Maasai bomas on overgrazed ground.
// The camps sit in the river's fig and fever-tree gallery; the west bank is the
// reserve - Talek Gate on the only road bridge, an anti-poaching outpost and a
// fly camp out on the open plain. Settlement kinds expand in
// lib/nextdayz_savanna.scad; land cover and hedges are drawn polygons.
PCG_SITES = [
    ["trading","Talek trading centre","sv_trading",[436,108],[190,46],96,20,2],
    ["mission","Talek primary school","sv_mission",[330,60],[110,80],0,6,1],
    ["clinic","Talek health centre","sv_clinic",[330,-70],[64,52],0,4,2],
    ["police","Talek police post","sv_post",[440,-110],[64,48],0,5,3],
    ["north_plots","Olare plots","sv_plots",[330,330],[110,84],0,9,1],
    ["south_plots","Riverside plots","sv_plots",[390,-300],[84,84],0,7,1],
    ["east_boma","Ole Sopia boma","sv_boma",[300,-440],[56,56],0,8,1],
    ["north_boma","Enkang Olkeju","sv_boma",[60,300],[53,53],0,7,1],
    ["mid_boma","Enkang Naibor","sv_boma",[-130,125],[50,50],0,6,1],
    ["shamba_n","Kisemei shamba","sv_shamba",[185,250],[46,44],10,4,1],
    ["shamba_s","River shamba","sv_shamba",[240,-200],[46,44],-20,3,1],
    ["gate","Talek Gate","sv_gate",[-435,-4],[64,60],0,6,3],
    ["river_camp","Mara river camp","sv_camp",[-400,262],[60,46],-90,4,2],
    ["croc_camp","Crocodile camp","sv_camp",[-150,-95],[80,46],0,6,2],
    ["outpost","Anti-poaching outpost","sv_post",[-300,-330],[60,48],20,4,3],
    ["fly_camp","Hunters' fly camp","sv_camp",[-40,-330],[44,46],90,3,2]
];

PCG_SPAWNS = [
    [-455,-420],
    [470,-430],
    [150,450],
    [-215,430],
    [465,440]
];

PCG_EXTRACTIONS = [
    [-180,-300],
    [-120,300]
];

PCG_PATHS = [
    ["road",[[310,186],[320,104]],5],
    ["road",[[446,5],[445,-84]],5],
    ["road",[[445,-40],[364,-62]],4],
    ["road",[[440,-136],[420,-256]],4],
    ["road",[[380,-344],[320,-410]],3.5],
    ["road",[[348,-280],[266,-210]],3.5],
    ["road",[[330,190],[330,286]],4],
    ["road",[[200,146],[190,226]],3.5],
    ["road",[[-83,244],[32,292]],3.5],
    ["road",[[-125,22],[-128,98]],3.5],
    ["road",[[-150,12],[-150,-70]],3.5],
    ["road",[[-440,-36],[-420,-160],[-330,-300]],3.5],
    ["road",[[-180,-300],[-66,-330]],3]
];

// Footbridges the camps strung over the river; the road bridge at the gate
// comes from OSM.
PCG_BRIDGES = [
    [[-134,-165],[-134,-213],3],
    [[140,-275],[78,-275],4],
    [[-325,100],[-388,100],3]
];

// Sparse grass tussocks and thorn on all open ground; stands come from PCG_LANDCOVER.
PCG_VEGETATION = [
    ["ruderal",[-480,-480,480,480],240,30,[0.8,1.3],"shrub"]
];

// [id, kind, [[x,y],...]]: savanna | acacia_woodland | gallery | thicket |
// shamba | grazing | banana. Gallery keeps only trees within 20 m of the river.
PCG_LANDCOVER = [
    ["wood_sw","acacia_woodland",[[-492,-492],[-240,-492],[-210,-410],[-360,-380],[-492,-330]]],
    ["plain_sw","savanna",[[-492,-330],[-360,-380],[-210,-410],[-240,-492],[60,-492],[120,-430],[90,-300],[60,-200],[-110,-215],[-240,-175],[-350,-60],[-370,40],[-492,40]]],
    ["wood_w","acacia_woodland",[[-492,40],[-370,40],[-365,130],[-400,180],[-492,200]]],
    ["nw_thicket","thicket",[[-492,330],[-420,340],[-380,420],[-400,492],[-492,492]]],
    ["north_grazing","grazing",[[-330,200],[-250,330],[-340,400],[-330,492],[-60,492],[-30,420],[-20,200]]],
    ["lugga_thicket","thicket",[[-150,380],[-20,385],[110,485],[492,480],[492,492],[90,492],[-40,430],[-150,440]]],
    ["meander_plain","savanna",[[-330,-40],[-240,-150],[-110,-190],[-10,-75],[140,-30],[150,60],[0,60],[-210,-20],[-300,-20]]],
    ["mid_grazing","grazing",[[-300,0],[-210,-5],[0,80],[40,200],[-20,200],[-330,200],[-340,60]]],
    ["shamba_n1","shamba",[[100,180],[150,200],[140,330],[95,330]]],
    ["shamba_n2","shamba",[[215,180],[262,190],[262,280],[215,280]]],
    ["shamba_n3","shamba",[[150,290],[262,290],[262,400],[150,400]]],
    ["shamba_s1","shamba",[[190,-150],[290,-140],[290,-108],[195,-112]]],
    ["shamba_s2","shamba",[[200,-250],[330,-260],[330,-330],[190,-330]]],
    ["banana_s","banana",[[160,-240],[195,-240],[195,-180],[170,-170]]],
    ["town_grazing","grazing",[[340,-492],[492,-492],[492,480],[395,480],[392,280],[262,280],[262,180],[300,120],[270,-90],[340,-110]]],
    ["se_plain","savanna",[[150,-492],[340,-492],[340,-340],[190,-340],[170,-420]]],
    ["east_bank","grazing",[[180,-100],[270,-90],[300,120],[262,175],[150,125],[160,60]]],
    ["gallery_0","gallery",[[49,-443],[116,-396],[134,-368],[98,-332],[69,-258],[104,-179],[129,-106],[154,-87],[25,-102],[-6,-121],[-41,-59],[10,-31],[175,-18],[199,-119],[171,-205],[141,-264],[159,-293],[205,-359],[168,-445],[90,-492]]],
    ["gallery_1","gallery",[[5,-112],[-30,-156],[-89,-213],[-184,-216],[-255,-181],[-313,-134],[-360,-69],[-383,-3],[-394,78],[-385,135],[-314,122],[-322,79],[-312,13],[-296,-36],[-261,-85],[-216,-120],[-168,-146],[-117,-147],[-84,-109],[-52,-68]]],
    ["gallery_2","gallery",[[-384,119],[-387,147],[-434,163],[-492,193],[-492,300],[-458,352],[-422,379],[-407,395],[-441,419],[-480,431],[-458,492],[-403,480],[-338,416],[-366,334],[-418,292],[-460,279],[-443,237],[-408,230],[-337,199],[-315,139]]]
];

// [[[x,y],...], species, spacing, verge offset]: sisal and euphorbia hedges
// along shamba boundaries, acacias shading the road into town.
PCG_ALLEYS = [
    [[[95,180],[95,330]],"sisal",5,1.0],
    [[[190,-332],[330,-332]],"sisal",5,1.0],
    [[[150,288],[262,288]],"euphorbia",12,1.0],
    [[[200,150],[400,205]],"acacia",24,8]
];

PCG_CLEAR = [
    [[-134,-150],[-134,-228],8],
    [[155,-275],[63,-275],8],
    [[-310,100],[-403,100],8]
];
