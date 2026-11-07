// Gameplay-authored PCG controls over a geographic reference. Metres; +y north.
// Site = [id,name,kind,center,size,rotation,buildingCount,risk(1..3)].
// Counts and routes are level-design decisions, never OSM footprint counts.
// Keep literal arrays here so the exporter can validate budgets and safe sites.
//
// Loket v4: the Ohře meander keeps the old town on its rock (square, lower
// street, castle), the north hill holds a green village (náves), the east bank
// carries spa villas and the porcelain works, a post-war suburb lines the SE road,
// a JZD farm and an army garrison sit on the southern plateau among fields, and
// chaty hide in the NW spruce woods. Settlement kinds expand in
// lib/nextdayz_settlement.scad; land cover and alleys are drawn polygons.
PCG_SITES = [
    ["old_town","Old town square","square",[40,40],[130,84],-8,22,2],
    ["castle","Castle Loket","hillcastle",[20,140],[52,60],0,3,3],
    ["lower_town","Lower town street","street",[0,-52],[130,44],-6,17,1],
    ["bridge_gate","Bridge roadblock","checkpoint",[-85,85],[52,44],0,2,2],
    ["clinic","Health centre","medical",[150,140],[66,60],0,3,2],
    ["north_village","North green village","naves",[190,400],[140,110],80,13,1],
    ["porcelain","Porcelain works","factory",[450,-85],[80,60],0,4,2],
    ["spa_villas","Spa villas","villas",[458,110],[110,56],90,6,1],
    ["se_suburb","SE suburb","suburb",[285,-170],[150,52],22,10,1],
    ["west_farm","West farmstead","homestead",[-205,290],[64,64],20,3,1],
    ["sw_farm","Field farmstead","homestead",[-420,-150],[60,62],0,3,1],
    ["jzd","Collective farm","jzd",[390,-360],[96,86],0,6,2],
    ["chaty","Weekend cottages","chaty",[-340,170],[100,90],0,8,1],
    ["radio","Forest radio post","military",[-430,290],[58,50],0,3,3],
    ["garrison","Southern garrison","military",[48,-440],[84,64],0,5,3]
];

PCG_SPAWNS = [
    [-460,-300],
    [-200,-265],
    [330,470],
    [-20,460],
    [370,20]
];

PCG_EXTRACTIONS = [
    [-265,360],
    [240,-300]
];

PCG_PATHS = [
    ["road",[[35,88],[22,112]],5],
    ["road",[[38,-8],[36,-30]],6],
    ["road",[[300,-250],[350,-290],[385,-318]],5],
    ["road",[[100,-302],[70,-360],[52,-408]],5],
    ["road",[[-420,338],[-428,316]],4],
    ["road",[[-305,95],[-330,128]],3.5],
    ["road",[[-311,-70],[-360,-110],[-418,-120]],4],
    ["road",[[352,168],[428,166]],5],
    ["road",[[400,-70],[412,-80]],5],
    ["road",[[160,320],[150,262]],5]
];

// Sparse ruderal shrubs on all open ground; stands come from PCG_LANDCOVER.
PCG_VEGETATION = [
    ["ruderal",[-480,-480,480,480],260,30,[0.9,1.4],"shrub"]
];

// [id, kind, [[x,y],...]]: spruce | mixed | beech | pine | beetle | riparian |
// orchard | field | meadow | clearing. Riparian keeps only trees near water.
PCG_LANDCOVER = [
    ["nw_spruce","spruce",[[-492,170],[-492,492],[-340,492],[-330,430],[-380,380],[-370,330],[-300,330],[-250,250],[-250,170]]],
    ["w_beetle","beetle",[[-492,-60],[-492,165],[-395,165],[-380,60],[-410,-60]]],
    ["w_spruce","spruce",[[-410,-60],[-380,60],[-395,165],[-250,165],[-250,100],[-290,60],[-300,-60]]],
    ["n_mixed","mixed",[[-180,250],[-180,345],[0,345],[60,300],[60,250],[0,250]]],
    ["ne_mixed","mixed",[[-20,360],[-60,492],[110,492],[120,420],[100,360]]],
    ["e_beech","beech",[[300,-110],[300,40],[380,45],[492,45],[492,-100],[400,-115]]],
    ["sw_spruce","spruce",[[-492,-200],[-492,-492],[-150,-492],[-140,-420],[-200,-330],[-260,-290],[-330,-230],[-420,-200]]],
    ["s_pine","pine",[[150,-400],[300,-400],[300,-492],[150,-492]]],
    ["field_a","field",[[-140,-262],[-60,-255],[-60,-380],[-140,-400]]],
    ["field_b","field",[[-60,-255],[20,-262],[0,-360],[-60,-380]]],
    ["meadow_c","meadow",[[20,-262],[160,-262],[160,-330],[0,-360]]],
    ["field_d","field",[[-150,-492],[-140,-400],[-60,-380],[0,-360],[0,-492]]],
    ["field_f","field",[[160,-262],[230,-250],[330,-280],[330,-310],[160,-330]]],
    ["field_g","field",[[440,-300],[492,-300],[492,-492],[340,-492],[340,-430]]],
    ["jzd_orchard","orchard",[[300,-430],[340,-430],[340,-490],[300,-490]]],
    ["field_ne","field",[[262,345],[262,480],[318,488],[340,420],[300,352]]],
    ["meadow_ne","meadow",[[110,262],[230,262],[262,300],[262,340],[110,330]]],
    ["village_orchard","orchard",[[140,268],[198,272],[202,312],[146,312]]],
    ["field_sw","field",[[-492,-200],[-420,-200],[-380,-215],[-380,-100],[-492,-100]]],
    ["loop_meadow","meadow",[[-240,-120],[-240,160],[-130,150],[-120,-110]]],
    ["river_loop","riparian",[[-300,-250],[-300,250],[-100,300],[60,280],[280,330],[330,250],[300,-150],[200,-260],[0,-260]]],
    ["river_north","riparian",[[-492,330],[-492,492],[-100,492],[-20,420],[60,380],[60,330],[-150,330]]],
    ["river_east","riparian",[[260,330],[300,492],[420,492],[492,430],[420,330],[360,250],[340,0],[360,-140],[300,-150],[290,0],[300,250]]]
];

// [[[x,y],...], species, spacing, verge offset]: fruit-tree and linden alleys.
PCG_ALLEYS = [
    [[[100,-302],[0,-284],[-100,-250],[-205,-207],[-226,-160]],"fruit",11,6.5],
    [[[300,-250],[350,-290]],"linden",14,5.5],
    [[[156,224],[140,300]],"linden",13,6],
    [[[396,-80],[341,0],[330,40],[350,165]],"linden",12,6],
    [[[-60,-255],[-60,-380]],"hedge",9,1.5],
    [[[160,-262],[160,-330]],"hedge",9,1.5],
    [[[262,345],[262,480]],"hedge",9,1.5]
];

PCG_CLEAR = [
    [[-160,245],[-90,117],10],
    [[330,339],[397,293],9]
];
