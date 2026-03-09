#ifndef POKEDEX_DATA_H
#define POKEDEX_DATA_H

#include <stdint.h>

/* 151 Pokedex descriptions - ASCII text with \n for line breaks */
static const char *pokedex_descriptions[151] = {
    "A strange seed was\nplanted on its\nback at birth.\nThe plant sprouts\nand grows with\nthis POKéMON.",  /* Bulbasaur */
    "When the bulb on\nits back grows\nlarge, it appears\nto lose the\nability to stand\non its hind legs.",  /* Ivysaur */
    "The plant blooms\nwhen it is\nabsorbing solar\nenergy. It stays\non the move to\nseek sunlight.",  /* Venusaur */
    "Obviously prefers\nhot places. When\nit rains, steam\nis said to spout\nfrom the tip of\nits tail.",  /* Charmander */
    "When it swings\nits burning tail,\nit elevates the\ntemperature to\nunbearably high\nlevels.",  /* Charmeleon */
    "Spits fire that\nis hot enough to\nmelt boulders.\nKnown to cause\nforest fires\nunintentionally.",  /* Charizard */
    "After birth, its\nback swells and\nhardens into a\nshell. Powerfully\nsprays foam from\nits mouth.",  /* Squirtle */
    "Often hides in\nwater to stalk\nunwary prey. For\nswimming fast, it\nmoves its ears to\nmaintain balance.",  /* Wartortle */
    "A brutal POKéMON\nwith pressurized\nwater jets on its\nshell. They are\nused for high\nspeed tackles.",  /* Blastoise */
    "Its short feet\nare tipped with\nsuction pads that\nenable it to\ntirelessly climb\nslopes and walls.",  /* Caterpie */
    "This POKéMON is\nvulnerable to\nattack while its\nshell is soft,\nexposing its weak\nand tender body.",  /* Metapod */
    "In battle, it\nflaps its wings\nat high speed to\nrelease highly\ntoxic dust into\nthe air.",  /* Butterfree */
    "Often found in\nforests, eating\nleaves.\nIt has a sharp\nvenomous stinger\non its head.",  /* Weedle */
    "Almost incapable\nof moving, this\nPOKéMON can only\nharden its shell\nto protect itself\nfrom predators.",  /* Kakuna */
    "Flies at high\nspeed and attacks\nusing its large\nvenomous stingers\non its forelegs\nand tail.",  /* Beedrill */
    "A common sight in\nforests and woods.\nIt flaps its\nwings at ground\nlevel to kick up\nblinding sand.",  /* Pidgey */
    "Very protective\nof its sprawling\nterritorial area,\nthis POKéMON will\nfiercely peck at\nany intruder.",  /* Pidgeotto */
    "When hunting, it\nskims the surface\nof water at high\nspeed to pick off\nunwary prey such\nas MAGIKARP.",  /* Pidgeot */
    "Bites anything\nwhen it attacks.\nSmall and very\nquick, it is a\ncommon sight in\nmany places.",  /* Rattata */
    "It uses its whis-\nkers to maintain\nits balance.\nIt apparently\nslows down if\nthey are cut off.",  /* Raticate */
    "Eats bugs in\ngrassy areas. It\nhas to flap its\nshort wings at\nhigh speed to\nstay airborne.",  /* Spearow */
    "With its huge and\nmagnificent wings,\nit can keep aloft\nwithout ever\nhaving to land\nfor rest.",  /* Fearow */
    "Moves silently\nand stealthily.\nEats the eggs of\nbirds, such as\nPIDGEY and\nSPEAROW, whole.",  /* Ekans */
    "It is rumored that\nthe ferocious\nwarning markings\non its belly\ndiffer from area\nto area.",  /* Arbok */
    "When several of\nthese POKéMON\ngather, their\nelectricity could\nbuild and cause\nlightning storms.",  /* Pikachu */
    "Its long tail\nserves as a\nground to protect\nitself from its\nown high voltage\npower.",  /* Raichu */
    "Burrows deep\nunderground in\narid locations\nfar from water.\nIt only emerges\nto hunt for food.",  /* Sandshrew */
    "Curls up into a\nspiny ball when\nthreatened. It\ncan roll while\ncurled up to\nattack or escape.",  /* Sandslash */
    "Although small,\nits venomous\nbarbs render this\nPOKéMON dangerous.\nThe female has\nsmaller horns.",  /* NidoranF */
    "The female`s horn\ndevelops slowly.\nPrefers physical\nattacks such as\nclawing and\nbiting.",  /* Nidorina */
    "Its hard scales\nprovide strong\nprotection. It\nuses its hefty\nbulk to execute\npowerful moves.",  /* Nidoqueen */
    "Stiffens its ears\nto sense danger.\nThe larger its\nhorns, the more\npowerful its\nsecreted venom.",  /* NidoranM */
    "An aggressive\nPOKéMON that is\nquick to attack.\nThe horn on its\nhead secretes a\npowerful venom.",  /* Nidorino */
    "It uses its\npowerful tail in\nbattle to smash,\nconstrict, then\nbreak the prey`s\nbones.",  /* Nidoking */
    "Its magical and\ncute appeal has\nmany admirers.\nIt is rare and\nfound only in\ncertain areas.",  /* Clefairy */
    "A timid fairy\nPOKéMON that is\nrarely seen. It\nwill run and hide\nthe moment it\nsenses people.",  /* Clefable */
    "At the time of\nbirth, it has\njust one tail.\nThe tail splits\nfrom its tip as\nit grows older.",  /* Vulpix */
    "Very smart and\nvery vengeful.\nGrabbing one of\nits many tails\ncould result in a\n1000-year curse.",  /* Ninetales */
    "When its huge eyes\nlight up, it sings\na mysteriously\nsoothing melody\nthat lulls its\nenemies to sleep.",  /* Jigglypuff */
    "The body is soft\nand rubbery. When\nangered, it will\nsuck in air and\ninflate itself to\nan enormous size.",  /* Wigglytuff */
    "Forms colonies in\nperpetually dark\nplaces. Uses\nultrasonic waves\nto identify and\napproach targets.",  /* Zubat */
    "Once it strikes,\nit will not stop\ndraining energy\nfrom the victim\neven if it gets\ntoo heavy to fly.",  /* Golbat */
    "During the day,\nit keeps its face\nburied in the\nground. At night,\nit wanders around\nsowing its seeds.",  /* Oddish */
    "The fluid that\noozes from its\nmouth isn`t drool.\nIt is a nectar\nthat is used to\nattract prey.",  /* Gloom */
    "The larger its\npetals, the more\ntoxic pollen it\ncontains. Its big\nhead is heavy and\nhard to hold up.",  /* Vileplume */
    "Burrows to suck\ntree roots. The\nmushrooms on its\nback grow by draw-\ning nutrients from\nthe bug host.",  /* Paras */
    "A host-parasite\npair in which the\nparasite mushroom\nhas taken over the\nhost bug. Prefers\ndamp places. ",  /* Parasect */
    "Lives in the\nshadows of tall\ntrees where it\neats insects. It\nis attracted by\nlight at night.",  /* Venonat */
    "The dust-like\nscales covering\nits wings are\ncolor coded to\nindicate the kinds\nof poison it has.",  /* Venomoth */
    "Lives about one\nyard underground\nwhere it feeds on\nplant roots. It\nsometimes appears\nabove ground.",  /* Diglett */
    "A team of DIGLETT\ntriplets.\nIt triggers huge\nearthquakes by\nburrowing 60 miles\nunderground.",  /* Dugtrio */
    "Adores circular\nobjects. Wanders\nthe streets on a\nnightly basis to\nlook for dropped\nloose change.",  /* Meowth */
    "Although its fur\nhas many admirers,\nit is tough to\nraise as a pet\nbecause of its\nfickle meanness.",  /* Persian */
    "While lulling its\nenemies with its\nvacant look, this\nwily POKéMON will\nuse psychokinetic\npowers.",  /* Psyduck */
    "Often seen swim-\nming elegantly by\nlake shores. It\nis often mistaken\nfor the Japanese\nmonster, Kappa.",  /* Golduck */
    "Extremely quick to\nanger. It could\nbe docile one\nmoment then\nthrashing away\nthe next instant.",  /* Mankey */
    "Always furious\nand tenacious to\nboot. It will not\nabandon chasing\nits quarry until\nit is caught.",  /* Primeape */
    "Very protective\nof its territory.\nIt will bark and\nbite to repel\nintruders from\nits space.",  /* Growlithe */
    "A POKéMON that\nhas been admired\nsince the past\nfor its beauty.\nIt runs agilely\nas if on wings.",  /* Arcanine */
    "Its newly grown\nlegs prevent it\nfrom running. It\nappears to prefer\nswimming than\ntrying to stand.",  /* Poliwag */
    "Capable of living\nin or out of\nwater. When out\nof water, it\nsweats to keep\nits body slimy.",  /* Poliwhirl */
    "An adept swimmer\nat both the front\ncrawl and breast\nstroke. Easily\novertakes the best\nhuman swimmers.",  /* Poliwrath */
    "Using its ability\nto read minds, it\nwill identify\nimpending danger\nand TELEPORT to\nsafety.",  /* Abra */
    "It emits special\nalpha waves from\nits body that\ninduce headaches\njust by being\nclose by.",  /* Kadabra */
    "Its brain can out-\nperform a super-\ncomputer.\nIts intelligence\nquotient is said\nto be 5,000.",  /* Alakazam */
    "Loves to build\nits muscles.\nIt trains in all\nstyles of martial\narts to become\neven stronger.",  /* Machop */
    "Its muscular body\nis so powerful, it\nmust wear a power\nsave belt to be\nable to regulate\nits motions.",  /* Machoke */
    "Using its heavy\nmuscles, it throws\npowerful punches\nthat can send the\nvictim clear over\nthe horizon. ",  /* Machamp */
    "A carnivorous\nPOKéMON that traps\nand eats bugs.\nIt uses its root\nfeet to soak up\nneeded moisture.",  /* Bellsprout */
    "It spits out\nPOISONPOWDER to\nimmobilize the\nenemy and then\nfinishes it with\na spray of ACID.",  /* Weepinbell */
    "Said to live in\nhuge colonies\ndeep in jungles,\nalthough no one\nhas ever returned\nfrom there.",  /* Victreebel */
    "Drifts in shallow\nseas. Anglers who\nhook them by\naccident are\noften punished by\nits stinging acid.",  /* Tentacool */
    "The tentacles are\nnormally kept\nshort. On hunts,\nthey are extended\nto ensnare and\nimmobilize prey.",  /* Tentacruel */
    "Found in fields\nand mountains.\nMistaking them\nfor boulders,\npeople often step\nor trip on them.",  /* Geodude */
    "Rolls down slopes\nto move. It rolls\nover any obstacle\nwithout slowing\nor changing its\ndirection.",  /* Graveler */
    "Its boulder-like\nbody is extremely\nhard. It can\neasily withstand\ndynamite blasts\nwithout damage.",  /* Golem */
    "Its hooves are 10\ntimes harder than\ndiamonds. It can\ntrample anything\ncompletely flat\nin little time.",  /* Ponyta */
    "Very competitive,\nthis POKéMON will\nchase anything\nthat moves fast\nin the hopes of\nracing it.",  /* Rapidash */
    "Incredibly slow\nand dopey. It\ntakes 5 seconds\nfor it to feel\npain when under\nattack.",  /* Slowpoke */
    "The SHELLDER that\nis latched onto\nSLOWPOKE`s tail\nis said to feed\non the host`s left\nover scraps.",  /* Slowbro */
    "Uses anti-gravity\nto stay suspended.\nAppears without\nwarning and uses\nTHUNDER WAVE and\nsimilar moves.",  /* Magnemite */
    "Formed by several\nMAGNEMITEs linked\ntogether. They\nfrequently appear\nwhen sunspots\nflare up.",  /* Magneton */
    "The sprig of\ngreen onions it\nholds is its\nweapon. It is\nused much like a\nmetal sword.",  /* Farfetchd */
    "A bird that makes\nup for its poor\nflying with its\nfast foot speed.\nLeaves giant\nfootprints.",  /* Doduo */
    "Uses its three\nbrains to execute\ncomplex plans.\nWhile two heads\nsleep, one head\nstays awake.",  /* Dodrio */
    "The protruding\nhorn on its head\nis very hard.\nIt is used for\nbashing through\nthick ice.",  /* Seel */
    "Stores thermal\nenergy in its\nbody. Swims at a\nsteady 8 knots\neven in intensely\ncold waters. ",  /* Dewgong */
    "Appears in filthy\nareas. Thrives by\nsucking up\npolluted sludge\nthat is pumped\nout of factories.",  /* Grimer */
    "Thickly covered\nwith a filthy,\nvile sludge. It\nis so toxic, even\nits footprints\ncontain poison.",  /* Muk */
    "Its hard shell\nrepels any kind\nof attack.\nIt is vulnerable\nonly when its\nshell is open.",  /* Shellder */
    "When attacked, it\nlaunches its\nhorns in quick\nvolleys. Its\ninnards have\nnever been seen.",  /* Cloyster */
    "Almost invisible,\nthis gaseous\nPOKéMON cloaks\nthe target and\nputs it to sleep\nwithout notice.",  /* Gastly */
    "Because of its\nability to slip\nthrough block\nwalls, it is said\nto be from an-\nother dimension.",  /* Haunter */
    "Under a full moon,\nthis POKéMON\nlikes to mimic\nthe shadows of\npeople and laugh\nat their fright.",  /* Gengar */
    "As it grows, the\nstone portions of\nits body harden\nto become similar\nto a diamond, but\ncolored black.",  /* Onix */
    "Puts enemies to\nsleep then eats\ntheir dreams.\nOccasionally gets\nsick from eating\nbad dreams.",  /* Drowzee */
    "When it locks eyes\nwith an enemy, it\nwill use a mix of\nPSI moves such as\nHYPNOSIS and\nCONFUSION.",  /* Hypno */
    "Its pincers are\nnot only powerful\nweapons, they are\nused for balance\nwhen walking\nsideways.",  /* Krabby */
    "The large pincer\nhas 10000 hp of\ncrushing power.\nHowever, its huge\nsize makes it\nunwieldy to use.",  /* Kingler */
    "Usually found in\npower plants.\nEasily mistaken\nfor a POKé BALL,\nthey have zapped\nmany people.",  /* Voltorb */
    "It stores electric\nenergy under very\nhigh pressure.\nIt often explodes\nwith little or no\nprovocation.",  /* Electrode */
    "Often mistaken\nfor eggs.\nWhen disturbed,\nthey quickly\ngather and attack\nin swarms.",  /* Exeggcute */
    "Legend has it that\non rare occasions,\none of its heads\nwill drop off and\ncontinue on as an\nEXEGGCUTE.",  /* Exeggutor */
    "Because it never\nremoves its skull\nhelmet, no one\nhas ever seen\nthis POKéMON`s\nreal face.",  /* Cubone */
    "The bone it holds\nis its key weapon.\nIt throws the\nbone skillfully\nlike a boomerang\nto KO targets.",  /* Marowak */
    "When in a hurry,\nits legs lengthen\nprogressively.\nIt runs smoothly\nwith extra long,\nloping strides.",  /* Hitmonlee */
    "While apparently\ndoing nothing, it\nfires punches in\nlightning fast\nvolleys that are\nimpossible to see. ",  /* Hitmonchan */
    "Its tongue can be\nextended like a\nchameleon`s. It\nleaves a tingling\nsensation when it\nlicks enemies.",  /* Lickitung */
    "Because it stores\nseveral kinds of\ntoxic gases in\nits body, it is\nprone to exploding\nwithout warning.",  /* Koffing */
    "Where two kinds\nof poison gases\nmeet, 2 KOFFINGs\ncan fuse into a\nWEEZING over many\nyears.",  /* Weezing */
    "Its massive bones\nare 1000 times\nharder than human\nbones. It can\neasily knock a\ntrailer flying.",  /* Rhyhorn */
    "Protected by an\narmor-like hide,\nit is capable of\nliving in molten\nlava of 3,600\ndegrees.",  /* Rhydon */
    "A rare and elusive\nPOKéMON that is\nsaid to bring\nhappiness to those\nwho manage to get\nit.",  /* Chansey */
    "The whole body is\nswathed with wide\nvines that are\nsimilar to sea-\nweed. Its vines\nshake as it walks.",  /* Tangela */
    "The infant rarely\nventures out of\nits mother`s\nprotective pouch\nuntil it is 3\nyears old.",  /* Kangaskhan */
    "Known to shoot\ndown flying bugs\nwith precision\nblasts of ink\nfrom the surface\nof the water.",  /* Horsea */
    "Capable of swim-\nming backwards by\nrapidly flapping\nits wing-like\npectoral fins and\nstout tail.",  /* Seadra */
    "Its tail fin\nbillows like an\nelegant ballroom\ndress, giving it\nthe nickname of\nthe Water Queen.",  /* Goldeen */
    "In the autumn\nspawning season,\nthey can be seen\nswimming power-\nfully up rivers\nand creeks.",  /* Seaking */
    "An enigmatic\nPOKéMON that can\neffortlessly\nregenerate any\nappendage it\nloses in battle.",  /* Staryu */
    "Its central core\nglows with the\nseven colors of\nthe rainbow. Some\npeople value the\ncore as a gem.",  /* Starmie */
    "If interrupted\nwhile it is\nmiming, it will\nslap around the\noffender with its\nbroad hands.",  /* MrMime */
    "With ninja-like\nagility and speed,\nit can create the\nillusion that\nthere is more\nthan one.",  /* Scyther */
    "It seductively\nwiggles its hips\nas it walks. It\ncan cause people\nto dance in\nunison with it.",  /* Jynx */
    "Normally found\nnear power plants,\nthey can wander\naway and cause\nmajor blackouts\nin cities.",  /* Electabuzz */
    "Its body always\nburns with an\norange glow that\nenables it to\nhide perfectly\namong flames.",  /* Magmar */
    "If it fails to\ncrush the victim\nin its pincers,\nit will swing it\naround and toss\nit hard.",  /* Pinsir */
    "When it targets\nan enemy, it\ncharges furiously\nwhile whipping its\nbody with its\nlong tails.",  /* Tauros */
    "In the distant\npast, it was\nsomewhat stronger\nthan the horribly\nweak descendants\nthat exist today.",  /* Magikarp */
    "Rarely seen in\nthe wild. Huge\nand vicious, it\nis capable of\ndestroying entire\ncities in a rage.",  /* Gyarados */
    "A POKéMON that\nhas been over-\nhunted almost to\nextinction. It\ncan ferry people\nacross the water.",  /* Lapras */
    "Capable of copying\nan enemy`s genetic\ncode to instantly\ntransform itself\ninto a duplicate\nof the enemy.",  /* Ditto */
    "Its genetic code\nis irregular.\nIt may mutate if\nit is exposed to\nradiation from\nelement STONEs.",  /* Eevee */
    "Lives close to\nwater. Its long\ntail is ridged\nwith a fin which\nis often mistaken\nfor a mermaid`s.",  /* Vaporeon */
    "It accumulates\nnegative ions in\nthe atmosphere to\nblast out 10000-\nvolt lightning\nbolts.",  /* Jolteon */
    "When storing\nthermal energy in\nits body, its\ntemperature could\nsoar to over 1600\ndegrees.",  /* Flareon */
    "A POKéMON that\nconsists entirely\nof programming\ncode. Capable of\nmoving freely in\ncyberspace.",  /* Porygon */
    "Although long\nextinct, in rare\ncases, it can be\ngenetically\nresurrected from\nfossils.",  /* Omanyte */
    "A prehistoric\nPOKéMON that died\nout when its\nheavy shell made\nit impossible to\ncatch prey.",  /* Omastar */
    "A POKéMON that\nwas resurrected\nfrom a fossil\nfound in what was\nonce the ocean\nfloor eons ago.",  /* Kabuto */
    "Its sleek shape is\nperfect for swim-\nming. It slashes\nprey with its\nclaws and drains\nthe body fluids.",  /* Kabutops */
    "A ferocious, pre-\nhistoric POKéMON\nthat goes for the\nenemy`s throat\nwith its serrated\nsaw-like fangs.",  /* Aerodactyl */
    "Very lazy. Just\neats and sleeps.\nAs its rotund\nbulk builds, it\nbecomes steadily\nmore slothful.",  /* Snorlax */
    "A legendary bird\nPOKéMON that is\nsaid to appear to\ndoomed people who\nare lost in icy\nmountains.",  /* Articuno */
    "A legendary bird\nPOKéMON that is\nsaid to appear\nfrom clouds while\ndropping enormous\nlightning bolts.",  /* Zapdos */
    "Known as the\nlegendary bird of\nfire. Every flap\nof its wings\ncreates a dazzling\nflash of flames.",  /* Moltres */
    "Long considered a\nmythical POKéMON\nuntil recently\nwhen a small\ncolony was found\nliving underwater.",  /* Dratini */
    "A mystical POKéMON\nthat exudes a\ngentle aura.\nHas the ability\nto change climate\nconditions.",  /* Dragonair */
    "An extremely\nrarely seen\nmarine POKéMON.\nIts intelligence\nis said to match\nthat of humans.",  /* Dragonite */
    "It was created by\na scientist after\nyears of horrific\ngene splicing and\nDNA engineering\nexperiments.",  /* Mewtwo */
    "So rare that it\nis still said to\nbe a mirage by\nmany experts. Only\na few people have\nseen it worldwide. "  /* Mew */
};

/* VWF character pixel widths (256 entries) */
static const uint8_t character_widths[256] = {
    0x05, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x08, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x08, 0x07, 0x07, 0x08, 0x07, 0x07,
    0x07, 0x08, 0x07, 0x07, 0x08, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x07, 0x08, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x08, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x08, 0x07, 0x07, 0x08,
    0x07, 0x07, 0x07, 0x08, 0x07, 0x07, 0x08, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x03, 0x07, 0x07, 0x03, 0x08, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x08, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x03,
    0x07, 0x05, 0x05, 0x05, 0x05, 0x05, 0x07, 0x07, 0x07, 0x07, 0x05, 0x07, 0x07, 0x07, 0x07, 0x07
};

/* Dex scroll bar Y offsets (151 entries) */
static const uint8_t dex_scroll_bar_offsets_data[151] = {
    0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05, 0x06,
    0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 0x08, 0x09, 0x09, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0C, 0x0C,
    0x0C, 0x0D, 0x0D, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x10, 0x10, 0x10, 0x11, 0x11, 0x12, 0x12, 0x12,
    0x13, 0x13, 0x14, 0x14, 0x14, 0x15, 0x15, 0x16, 0x16, 0x16, 0x17, 0x17, 0x18, 0x18, 0x18, 0x19,
    0x19, 0x1A, 0x1A, 0x1A, 0x1B, 0x1B, 0x1C, 0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1E, 0x1F, 0x1F,
    0x20, 0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x22, 0x23, 0x23, 0x24, 0x24, 0x24, 0x25, 0x25, 0x26,
    0x26, 0x26, 0x27, 0x27, 0x28, 0x28, 0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2A, 0x2B, 0x2B, 0x2C, 0x2C,
    0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2E, 0x2F, 0x2F, 0x30, 0x30, 0x30, 0x31, 0x31, 0x32, 0x32, 0x32,
    0x33, 0x33, 0x34, 0x34, 0x34, 0x35, 0x35, 0x36, 0x36, 0x36, 0x37, 0x37, 0x38, 0x38, 0x38, 0x39,
    0x39, 0x3A, 0x3A, 0x3A, 0x3B, 0x3B, 0x3C
};

/* Pokedex height/weight attributes - auto-generated from pokedex_mon_attributes.asm.
 * weight_decimal: 0 for normal, non-zero for fractional (e.g. 2 = 0.2 lbs). */
static const struct {
    uint8_t feet;
    uint8_t inches;
    uint16_t weight;
    uint8_t weight_decimal;
} pokedex_attributes[151] = {
    { 2,  4,   15, 0}, { 3,  3,   29, 0}, { 6,  7,  221, 0}, { 2,  0,   19, 0},
    { 3,  7,   42, 0}, { 5,  7,  200, 0}, { 1,  8,   20, 0}, { 3,  3,   50, 0},
    { 5,  3,  189, 0}, { 1,  0,    6, 0}, { 2,  4,   22, 0}, { 3,  7,   71, 0},
    { 1,  0,    7, 0}, { 2,  0,   22, 0}, { 3,  3,   65, 0}, { 1,  0,    4, 0},
    { 3,  7,   66, 0}, { 4, 11,   87, 0}, { 1,  0,    8, 0}, { 2,  4,   41, 0},
    { 1,  0,    4, 0}, { 3, 11,   84, 0}, { 6,  7,   15, 0}, {11,  6,  143, 0},
    { 1,  4,   13, 0}, { 2,  7,   66, 0}, { 2,  0,   26, 0}, { 3,  3,   65, 0},
    { 1,  4,   15, 0}, { 2,  7,   44, 0}, { 4,  3,  132, 0}, { 1,  8,   20, 0},
    { 2, 11,   43, 0}, { 4,  7,  137, 0}, { 2,  0,   17, 0}, { 4,  3,   88, 0},
    { 2,  0,   22, 0}, { 3,  7,   44, 0}, { 1,  8,   12, 0}, { 3,  3,   26, 0},
    { 2,  7,   17, 0}, { 5,  3,  121, 0}, { 1,  8,   12, 0}, { 2,  7,   19, 0},
    { 3, 11,   41, 0}, { 1,  0,   12, 0}, { 3,  3,   65, 0}, { 3,  3,   66, 0},
    { 4, 11,   28, 0}, { 0,  8,    2, 0}, { 2,  4,   73, 0}, { 1,  4,    9, 0},
    { 3,  3,   71, 0}, { 2,  7,   43, 0}, { 5,  7,  169, 0}, { 1,  8,   62, 0},
    { 3,  3,   71, 0}, { 2,  4,   42, 0}, { 6,  3,  342, 0}, { 2,  0,   27, 0},
    { 3,  3,   44, 0}, { 4,  3,  119, 0}, { 2, 11,   43, 0}, { 4,  3,  125, 0},
    { 4, 11,  106, 0}, { 2,  7,   43, 0}, { 4, 11,  155, 0}, { 5,  3,  287, 0},
    { 2,  4,    9, 0}, { 3,  3,   14, 0}, { 5,  7,   34, 0}, { 2, 11,  100, 0},
    { 5,  3,  121, 0}, { 1,  4,   44, 0}, { 3,  3,  232, 0}, { 4,  7,  662, 0},
    { 3,  3,   66, 0}, { 5,  7,  209, 0}, { 3, 11,   79, 0}, { 5,  3,  173, 0},
    { 1,  0,   13, 0}, { 3,  3,  132, 0}, { 2,  7,   33, 0}, { 4,  7,   86, 0},
    { 5, 11,  188, 0}, { 3,  7,  198, 0}, { 5,  7,  265, 0}, { 2, 11,   66, 0},
    { 3, 11,   66, 0}, { 1,  0,    9, 0}, { 4, 11,  292, 0}, { 4,  3,    0, 2},
    { 5,  3,    0, 2}, { 4, 11,   89, 0}, {28, 10,  463, 0}, { 3,  3,   71, 0},
    { 5,  3,  167, 0}, { 1,  4,   14, 0}, { 4,  3,  132, 0}, { 1,  8,   23, 0},
    { 3, 11,  147, 0}, { 1,  4,    6, 0}, { 6,  7,  265, 0}, { 1,  4,   14, 0},
    { 3,  3,   99, 0}, { 4, 11,  110, 0}, { 4,  7,  111, 0}, { 3, 11,  144, 0},
    { 2,  0,    2, 0}, { 3, 11,   21, 0}, { 3,  3,  254, 0}, { 6,  3,  265, 0},
    { 3,  7,   76, 0}, { 3,  3,   77, 0}, { 7,  3,  176, 0}, { 1,  4,   18, 0},
    { 3, 11,   55, 0}, { 2,  0,   33, 0}, { 4,  3,   86, 0}, { 2,  7,   76, 0},
    { 3,  7,  176, 0}, { 4,  3,  120, 0}, { 4, 11,  123, 0}, { 4,  7,   90, 0},
    { 3,  7,   66, 0}, { 4,  3,   98, 0}, { 4, 11,  121, 0}, { 4,  7,  195, 0},
    { 2, 11,   22, 0}, {21,  4,  518, 0}, { 8,  2,  485, 0}, { 1,  0,    9, 0},
    { 1,  0,   14, 0}, { 3,  3,   64, 0}, { 2,  7,   54, 0}, { 2, 11,   55, 0},
    { 2,  7,   80, 0}, { 1,  4,   17, 0}, { 3,  3,   77, 0}, { 1,  8,   25, 0},
    { 4,  3,   89, 0}, { 5, 11,  130, 0}, { 6, 11, 1014, 0}, { 5,  7,  122, 0},
    { 5,  3,  116, 0}, { 6,  7,  132, 0}, { 5, 11,    7, 0}, {13,  1,   36, 0},
    { 7,  3,  463, 0}, { 6,  7,  269, 0}, { 1,  4,    9, 0},
};

#endif
