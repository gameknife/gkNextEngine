// Qinglong Lake, Chengdu: geography is the skeleton; 48 buildings are a game-design budget.
// Site=[id,name,kind,center,padSize,rotation,buildingCount,risk]. Metres; +y north.

PCG_SITES = [
    ["visitor","Abandoned visitor centre","visitor",[-70,365],[96,64],0,5,1],
    ["bamboo_garden","Bamboo tea garden","park",[-280,310],[76,60],0,4,1],
    ["ranger","Quarantine ranger station","military",[-365,410],[72,56],0,3,3],
    ["greenway_kiosk","Overgrown greenway rest stop","park",[-55,205],[64,48],0,3,1],
    ["tea_court","Lakeside tea courtyard","park",[-250,75],[80,60],0,4,2],
    ["west_cafe","Ransacked west-shore cafe","visitor",[-422,-255],[76,70],0,4,2],
    ["maintenance","Park maintenance yard","industry",[-275,-35],[76,58],0,4,2],
    ["clinic","Emergency aid station","medical",[-80,-60],[70,54],0,4,2],
    ["old_village","Abandoned courtyard homes","village",[-125,-315],[84,64],0,6,1],
    ["fishery","Deserted fishery workshops","farm",[250,-290],[80,58],0,4,1],
    ["south_logistics","South service compound","industry",[365,-415],[78,62],0,4,2],
    ["east_checkpoint","East greenway cordon","checkpoint",[335,-105],[64,50],0,3,2]
];

PCG_SPAWNS = [
    [-440,470],
    [70,450],
    [-440,-440],
    [-25,-455],
    [450,-310]
];

PCG_EXTRACTIONS = [
    [-345,70],
    [400,-245]
];

PCG_PATHS = [
    ["road",[[-440,470],[-405,445]],4],
    ["road",[[-365,378],[-330,350],[-280,344]],4],
    ["road",[[-238,310],[-185,340],[-122,365]],4],
    ["road",[[70,450],[0,430],[-70,401]],4],
    ["road",[[-70,329],[-55,300],[-55,233]],4],
    ["road",[[-280,276],[-250,230],[-200,210],[-170,170],[-140,120]],4],
    ["road",[[-140,120],[-180,50],[-205,75]],4],
    ["road",[[-250,40],[-260,10],[-275,-2]],4],
    ["road",[[-275,-68],[-340,-95],[-410,-140],[-422,-216]],4],
    ["road",[[-275,-35],[-180,-60],[-119,-60]],5],
    ["road",[[-40,-60],[60,-60],[170,-80],[280,-80],[235,-170],[250,-257]],4],
    ["road",[[-422,-294],[-440,-360],[-440,-440],[-320,-425],[-220,-410],[-125,-351]],4],
    ["road",[[-125,-279],[-70,-240],[-25,-220]],4],
    ["road",[[-25,-455],[-30,-405],[-100,-360]],4],
    ["road",[[250,-323],[280,-365],[322,-415]],5],
    ["road",[[365,-380],[400,-320],[400,-245],[450,-310]],5],
    ["road",[[335,-135],[315,-130],[300,-105],[280,-80]],4],
    ["road",[[-55,177],[-100,180],[-140,120]],4],
    ["road",[[-405,445],[-380,380],[-392,344]],4],
    ["road",[[-420,313],[-420,230],[-360,150],[-345,70],[-335,5],[-315,-35]],4]
];

PCG_BRIDGES = [
    [[-140,120],[-180,50],4],
    [[280,-80],[235,-170],4]
];

PCG_VEGETATION = [
    ["northern_woodland",[-470,170,90,480],420,28,[1.15,1.8],"tree"],
    ["southern_groves",[-480,-480,470,-180],280,26,[1.1,1.7],"tree"],
    ["lakeside_willows",[-450,-180,480,180],180,26,[1.0,1.6],"tree"],
    ["bamboo_covers",[-450,110,50,420],150,28,[1.0,1.6],"bamboo"],
    ["reclaimed_lots",[-480,-480,480,480],600,30,[0.9,1.5],"shrub"],
    ["dead_park_trees",[-450,-450,460,470],55,28,[1.2,1.7],"dead"]
];

PCG_CLEAR = [
    [[-140,120],[-180,50],12],
    [[280,-80],[235,-170],12],
    [[-430,320],[-385,350],10]
];
