// Gameplay-authored PCG controls over a geographic reference. Metres; +y north.
// Site = [id,name,kind,center,padSize,rotation,buildingCount,risk(1..3)].
// Counts and routes are level-design decisions, never OSM footprint counts.
// Keep literal arrays here so the exporter can validate budgets and safe sites.
PCG_SITES = [
    ["castle","Castle quarantine","castle",[20,140],[80,68],0,4,3],
    ["old_quarter","Old quarter","village",[50,40],[100,68],0,8,1],
    ["market","Ransacked market","market",[130,-40],[84,64],0,6,2],
    ["clinic","Emergency clinic","medical",[145,140],[66,60],0,3,2],
    ["workshops","Riverside workshops","industry",[-80,20],[72,56],0,4,2],
    ["bridge_gate","Bridge roadblock","checkpoint",[-85,85],[52,44],0,2,2],
    ["west_hamlet","West hamlet","village",[-195,285],[76,60],0,6,1],
    ["radio","Forest radio post","military",[-355,190],[58,50],0,3,3],
    ["farm","Abandoned farm","farm",[-280,-360],[76,64],0,5,1],
    ["logistics","East logistics yard","industry",[430,120],[76,66],0,5,2],
    ["military","Southern garrison","military",[300,-290],[84,68],0,5,3],
    ["north_post","North road outpost","checkpoint",[180,350],[76,60],0,5,2],
    ["river_camp","River salvage camp","farm",[145,-240],[58,48],0,4,1]
];

PCG_SPAWNS = [
    [-430,330],
    [-380,-210],
    [70,390],
    [430,-310],
    [150,250]
];

PCG_EXTRACTIONS = [
    [-265,360],
    [430,-150]
];

PCG_PATHS = [
    ["road",[[-355,190],[-285,260],[-195,285],[-155,248]],5],
    ["road",[[-92,117],[-85,85],[-80,20],[0,40]],6],
    ["road",[[20,75],[20,102]],5],
    ["road",[[105,40],[140,5]],6],
    ["road",[[70,140],[109,140]],5],
    ["road",[[145,174],[150,250],[180,316]],5],
    ["road",[[218,350],[285,350],[331,339]],6],
    ["road",[[397,293],[430,230],[430,157]],6],
    ["road",[[430,83],[440,0],[430,-150],[350,-230],[300,-252]],6],
    ["road",[[-280,-324],[-310,-280],[-340,-220],[-385,-120]],5],
    ["road",[[130,-76],[145,-110],[128,-145]],5],
    ["road",[[145,-179],[145,-212]],5]
];

PCG_VEGETATION = [
    ["west_woods",[-480,-430,-230,430],360,30,[1.6,2.7],"tree"],
    ["east_woods",[280,-470,480,460],260,30,[1.6,2.6],"tree"],
    ["river_cover",[-220,-250,260,370],220,26,[1.5,2.3],"tree"],
    ["reclaimed_lots",[-480,-480,480,480],420,32,[0.9,1.6],"shrub"],
    ["dead_woods",[-470,-400,470,430],72,30,[1.5,2.3],"dead"]
];

PCG_CLEAR = [
    [[-160,245],[-90,117],10],
    [[330,339],[397,293],9]
];
