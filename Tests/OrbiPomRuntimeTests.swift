import AppKit
import CryptoKit

/// Original rules and solver checks in disposable JavaScriptCore VMs. No app
/// preferences, native window, network, timers, files from Downloads, or audio.
enum OrbiPomRuntimeTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let game = try! OrbiPomRuntime()
        func js(_ expression: String, _ message: String) {
            let value = game.evaluateForTesting(expression)?.toBool() ?? false
            check(game.error == nil && value, message + (game.error.map { ": " + $0 } ?? ""))
        }
        let initial = game.start(seed: 123)
        check(initial.isPlaying && initial.score == 0 && initial.bodies.isEmpty, "An offline run starts without prior game/user state")
        js("Matter.version === '0.20.0'", "The original Matter0.20.0 engine runs in JavaScriptCore")
        js("__game.phys.engine.positionIterations===12 && __game.phys.engine.velocityIterations===7 && __game.phys.engine.constraintIterations===3 && __game.phys.engine.enableSleeping", "Original solver iteration and sleeping parameters are retained")
        js("__game.phys.engine.gravity.y===1.05 && ey.body.friction===1 && ey.body.frictionStatic===2 && ey.body.restitution===0 && ey.body.density===.002 && ey.body.slop===.042", "Original gravity and contact coefficients are retained")
        js("typeof fetch==='undefined' && typeof XMLHttpRequest==='undefined' && typeof Audio==='undefined' && typeof WebSocket==='undefined'", "Offline VM exposes no network or audio capability")
        let source = try! Data(contentsOf: HUDResources.url(for: "OrbiPom/matter-0.20.0.js")!)
        check(SHA256.hash(data: source).map { String(format: "%02x", $0) }.joined() == "928d059868201b2c4c270818fda44e5d26c099e4df407043f018f9bbe2c598cf", "The bundled Matter factory is byte-identical to the supplied original")
        for level in 1...11 { check(OrbiPomArtwork.image(level: level)?.isValid == true, "Every original OrbiPom sprite decodes") }
        for skill in OrbiPomSkill.allCases { check(OrbiPomArtwork.skill(skill)?.isValid == true, "Every original skill icon decodes") }
        check(OrbiPomArtwork.image(level: 0) == nil && OrbiPomArtwork.image(level: 12) == nil, "Out-of-range artwork cannot resolve another resource")
        game.evaluateForTesting("var fixtureProfiles = " + profiles + "; function closeNumber(a,b){return Math.abs(a-b)<1e-9;}function shapeEqual(a,b){if(typeof b==='number')return closeNumber(a,b);if(Array.isArray(b))return a.length===b.length&&b.every((v,i)=>shapeEqual(a[i],v));if(b&&typeof b==='object')return Object.keys(b).every(k=>shapeEqual(a[k],b[k]));return a===b;}")
        for level in 1...11 {
            js("shapeEqual(eM(\(level)).collision,fixtureProfiles[\(level - 1)].collision)", "Level\(level) retains every original normalized circle/polygon coordinate and bound")
            js("(()=>{var p=__game.phys.spawn(\(level),115,140);var good=p.collisionParts.length===fixtureProfiles[\(level - 1)].parts&&p.body.sleepThreshold===180&&Math.abs(p.body.mass-p.collisionParts[0].body.mass)<1e-9&&Math.abs(p.body.inertia-p.collisionParts[0].body.inertia)<1e-9;__game.phys.remove(p.id);return good;})()", "Level\(level) compound mass/inertia use the source primary part rather than a circle approximation")
        }
        game.start(seed: 10)
        check(game.drop() && !game.drop(), "A drop is accepted once and original500ms cooldown rejects immediate repeat")
        for _ in 0..<29 { game.advance(seconds: 1 / 60) }
        check(!game.drop(), "Drop remains locked before500ms")
        for _ in 0..<2 { game.advance(seconds: 1 / 60) }
        check(game.drop(), "Drop becomes available after500ms")
        game.movePointer(x: -30, y: 40)
        let beforeOutside = game.snapshot.bodies.count
        game.pointerUp(x: -30, y: 40)
        check(game.snapshot.bodies.count == beforeOutside, "Releasing outside the vessel cannot drop a body")
        js("(()=>{__game.spawnHistory=[];var last=[],seen=new Set;for(var i=0;i<10000;i++){var n=__game.nextSpawnLevel();if(n<1||n>5||last.length===2&&last[0]===n&&last[1]===n)return false;seen.add(n);last.push(n);if(last.length>2)last.shift();}return seen.size===5;})()", "Spawn remains1...5 and prevents a third consecutive equal level across10000rolls")
        js("(()=>{var c=new sY();return c.takeSteps(5000)===3&&c.takeSteps(0)===0&&c.takeSteps(NaN)===0&&c.takeSteps(-1)===0;})()", "Fixed60Hz clock caps long stalls to three steps and rejects invalid elapsed values")
        js("(()=>{var c=new sY(),n=0;for(var i=0;i<60;i++)n+=c.takeSteps(1000/60);return n===60;})()", "60render ticks execute60original physics steps")
        js("(()=>{var c=new sY(),n=0;for(var i=0;i<30;i++)n+=c.takeSteps(1000/30);return n===60;})()", "30render ticks retain60original physics steps")
        game.start(seed: 40)
        js("(()=>{var a=__game.phys.spawn(1,110,140),b=__game.phys.spawn(1,116,140);__game.tick(1000/60,__now);return e5().score===1&&e5().mergeCount===1&&Array.from(__game.phys.bodies.values()).some(b=>b.level===2);})()", "Real Matter collision between equal compound bodies merges once and awards source-level score")
        game.start(seed: 40)
        js("(()=>{var a=__game.phys.spawn(1,110,140),b=__game.phys.spawn(1,116,140),c=__game.phys.spawn(1,112,135);__game.tick(1000/60,__now);return e5().score===1&&e5().mergeCount===1&&__game.phys.bodies.size===2;})()", "Simultaneous contacts queue each source body at most once")
        for level in 1...11 {
            game.start(seed: 55)
            js("(()=>{var a=__game.phys.spawn(\(level),90,160),b=__game.phys.spawn(\(level),140,160);__game.pendingMerges.push({a:a.body,b:b.body,level:\(level)});__game.processMerges();return e5().score===fixtureProfiles[\(level - 1)].score&&e5().mergeCount===1&&(__game.phys.bodies.size===\(level == 11 ? 0 : 1))&&Array.from(__game.phys.bodies.values()).every(b=>b.level===\(min(11, level + 1)));})()", "Source-level\(level) score and merge advancement match original; maximum pair vanishes without level12")
        }
        game.start(seed: 2)
        js("(()=>{for(var i=0;i<36;i++)e5().onMerge(2,1);return e5().energy===3&&e5().energyProgress===0&&e5().score===36;})()", "Twelve merges grant each energy point up to three without combo score multiplication")
        js("(()=>{e5().addScore(99999);e5().onMerge(11,10);return e5().score===99999;})()", "Scores saturate at99999")
        game.start(seed: 1)
        js("(()=>{for(var i=0;i<29;i++)e5().onMerge(2,1);__now+=1999;e5().tickEnergyDecay(__now);if(e5().energyDecayStartedAt!==0)return false;__now+=1;e5().tickEnergyDecay(__now);__now+=2500;return e5().energy===2&&Math.abs(e2(e5(),__now)-2.5)<1e-9;})()", "Only two-point partial energy starts continuousone-slot/second decay after2s")
        game.start(seed: 4)
        for skill in OrbiPomSkill.allCases { check(!game.activate(skill), "An uncharged skill cannot activate") }
        game.evaluateForTesting("e5().addEnergy(3);var target=__game.phys.spawn(4,115,130);")
        check(game.activate(.clear) && game.snapshot.skillPhase == "selecting", "Clear enters original target-selection phase")
        game.movePointer(x: 115, y: 130)
        check(game.snapshot.hoverBodyID != nil, "Clear resolves exact compound-body and visual target hit")
        game.pointerUp(x: 115, y: 130)
        check(game.snapshot.energy == 2 && game.snapshot.swapCharge == 1 && game.snapshot.skillPhase == "casting" && game.snapshot.bodies.allSatisfy { $0.id < 0 }, "Clear spendsone energy, charges swap and removes its target immediately")
        for _ in 0..<40 { game.advance(seconds: 1 / 60) }
        check(game.snapshot.skill == nil, "Clear cast finishes after650ms")
        game.evaluateForTesting("__game.phys.spawn(4,115,235)")
        check(game.activate(.wind), "Wind accepts two available energy points")
        game.pointerUp(x: 100, y: 130)
        check(game.snapshot.energy == 0 && game.snapshot.swapCharge == 3 && game.snapshot.skillPhase == "casting", "Wind begins on confirmation and charges swap bytwo")
        js("__game.phys.engine.enableSleeping===false && __game.phys.skillCeiling!==null", "Wind disables sleeping and uses original temporary ceiling")
        for _ in 0..<80 { game.advance(seconds: 1 / 60) }
        check((game.snapshot.windSurfaceY ?? 280) < 100, "Wind fills to original75percent depth with compound buoyancy")
        for _ in 0..<400 { game.advance(seconds: 1 / 60) }
        check(game.snapshot.skill == nil && game.snapshot.windSurfaceY == nil, "Wind fill/hold/drain completes at original schedule")
        js("__game.phys.engine.enableSleeping===true&&__game.phys.skillCeiling===null&&Array.from(__game.phys.bodies.values()).every(b=>b.body.frictionAir===ey.body.frictionAir)", "Wind restores sleeping, ceiling and per-body friction without leaving background activity")
        game.evaluateForTesting("e5().addEnergy(3)")
        check(game.activate(.shake), "Shake accepts three energy points")
        game.pointerUp(x: 100, y: 130)
        check(game.snapshot.energy == 0 && game.snapshot.swapCharge == 6, "Shake spends3and caps swap charge at6")
        for _ in 0..<121 { game.advance(seconds: 1 / 60) }
        check(game.snapshot.skill == nil, "Shake finishes original2000mscast")
        game.evaluateForTesting("__game.phys.clearBodies();var left=__game.phys.spawn(3,60,150),right=__game.phys.spawn(6,165,180);var oldLeft={x:left.body.position.x,y:left.body.position.y};")
        check(game.activate(.swap), "Six-spent-energy swap unlocks without ordinary energy")
        game.pointerUp(x: 60, y: 150)
        check(game.snapshot.selectedBodyIDs.count == 1, "Swap keeps first distinct target armed")
        game.pointerUp(x: 165, y: 180)
        check(game.snapshot.skillPhase == "casting" && game.snapshot.swapCharge == 6, "Swap suspends two bodies and defers charge settlement until completion")
        js("__game.phys.suspendedIds.size===2", "Both selected bodies are removed from normal physics during swap")
        for _ in 0..<31 { game.advance(seconds: 1 / 60) }
        check(game.snapshot.swapCharge == 6, "Swap does not consume charge at movement start")
        for _ in 0..<50 { game.advance(seconds: 1 / 60) }
        check(game.snapshot.skill == nil && game.snapshot.swapCharge == 0, "Swap completes original1300msBezierexchange and settles charge")
        js("__game.phys.suspendedIds.size===0", "Swap resumes both physics bodies")
        game.evaluateForTesting("e3.setState({swapCharge:6});")
        check(game.activate(.swap), "Swap can select again after recharge")
        let targets = game.snapshot.bodies.filter { $0.id > 0 }
        game.pointerUp(x: targets[0].x, y: targets[0].y); game.pointerUp(x: targets[1].x, y: targets[1].y)
        game.cancelSkill()
        js("__game.phys.suspendedIds.size===0&&e5().swapCharge===6", "Cancelling an unfinished swap restores kinematics without spending charge")
        game.start(seed: 6)
        js("(()=>{var d=new sF();if(d.update(true,0,5000)!==undefined||d.update(true,119,5000)!==undefined||d.update(true,120,5000)!==5000)return false;d.pause(200);d.resume(1200);return d.update(true,1320,5000)===4800;})()", "Danger requires120mssettled violation and pause shifts countdown rather than consuming hidden time")
        js("(()=>{var d=new sF();d.update(true,0,5000);d.update(true,120,5000);if(d.update(false,130,5000)!==undefined||d.update(false,349,5000)!==undefined)return false;return d.update(false,350,5000)===null&&!d.isTracking();})()", "Danger clears only after220mscontinuous safe placement")
        js("(()=>{var d=__game.phys.spawn(5,115,0);Matter.Body.setStatic(d.body,true);for(var i=0;i<320;i++){__now+=1000/60;__game.checkGameOver(__now);}return e5().state==='over';})()", "A persistent settledoverflow ends the run after original5scountdown")
        game.start(seed: 44); game.drop(); game.advance(seconds: 1 / 60)
        game.setPaused(true)
        let frozen = game.snapshot, ticks = game.advanceCount
        for _ in 0..<200 { game.advance(seconds: 60) }
        check(game.snapshot == frozen && game.advanceCount == ticks, "Hidden/paused runtime performs zero simulation or timer work across repeated host calls")
        game.setPaused(false); game.advance(seconds: 1 / 60)
        check(game.snapshot.simulationTime - frozen.simulationTime < 0.02, "Reopening resumes without catch-up of hidden time")
        let after = game.snapshot
        game.advance(seconds: .nan); game.advance(seconds: -.infinity); game.movePointer(x: .nan, y: 20); game.pointerUp(x: 20, y: .infinity)
        check(game.snapshot == after && game.error == nil, "Nonfinite input cannot corrupt the engine")
        game.setHighScore(124); game.start(seed: 12)
        check(game.snapshot.highScore == 124, "Starting another run preserves the injected local highscore")
        let sixty = try! OrbiPomRuntime(), thirty = try! OrbiPomRuntime()
        for runtime in [sixty, thirty] { runtime.start(seed: 12345) }
        for second in 0..<10 {
            for runtime in [sixty, thirty] { runtime.movePointer(x: Double(30 + (second * 47) % 160), y: 40); runtime.drop() }
            for _ in 0..<60 { sixty.advance(seconds: 1 / 60) }
            for _ in 0..<30 { thirty.advance(seconds: 1 / 30) }
        }
        check(sixty.snapshot.score == thirty.snapshot.score && sixty.snapshot.bodies.count == thirty.snapshot.bodies.count, "30/60Hzpresentation preserves equivalent deterministicdrop/score scenarios")
        for (a,b) in zip(sixty.snapshot.bodies.filter { $0.id > 0 }, thirty.snapshot.bodies.filter { $0.id > 0 }) {
            check(a.level == b.level && abs(a.x-b.x)<1e-6 && abs(a.y-b.y)<1e-6 && abs(a.angle-b.angle)<1e-6, "30/60Hzpresentation yields equal fixed-step compound-body transforms")
        }
        check(sixty.error == nil && thirty.error == nil && game.error == nil, "All original skills and solver paths complete withoutJavaScript exceptions")
        OrbiPomArtwork.releaseDecodedImages()
        return count
    }
    private static let profiles = #"""
[{"collision":{"primary":{"kind":"circle","x":-0.057523199999999976,"y":2.070048,"radius":7.9409088},"extras":[{"kind":"polygon","points":[{"x":-8.200742400000001,"y":-4.6350527999999995},{"x":-3.9836735999999995,"y":-9.5311488},{"x":-0.23118720000000048,"y":-5.814393600000001},{"x":-6.3781056,"y":-2.3120832}]},{"kind":"polygon","points":[{"x":3.6642624,"y":-9.495417600000001},{"x":8.23872,"y":-4.3848768},{"x":8.1315072,"y":-1.4543615999999997},{"x":-0.12397439999999982,"y":-5.778662400000001}]}],"bounds":{"minX":-8.200742400000001,"minY":-9.5311488,"maxX":8.23872,"maxY":10.010956799999999}},"parts":3,"score":1},{"collision":{"primary":{"kind":"circle","x":-0.061311999999999506,"y":2.2974464000000014,"radius":10.7148544},"extras":[{"kind":"polygon","points":[{"x":-9.3652992,"y":-2.7315712000000008},{"x":-6.3428608,"y":-12.0278016},{"x":-0.11297280000000001,"y":-12.732492800000001},{"x":5.430912000000001,"y":-12.096435200000002},{"x":9.851392000000002,"y":-2.2554112000000006}]}],"bounds":{"minX":-10.7761664,"minY":-12.732492800000001,"maxX":10.653542400000001,"maxY":13.012300800000002}},"parts":2,"score":3},{"collision":{"primary":{"kind":"circle","x":-0.2951359999999994,"y":3.2349759999999996,"radius":12.686880000000002},"extras":[{"kind":"polygon","points":[{"x":-7.115968000000001,"y":-15.230048},{"x":-0.2066239999999997,"y":-9.273728},{"x":-4.435616,"y":7.225312},{"x":-15.693088000000001,"y":-3.5556479999999997}]},{"kind":"polygon","points":[{"x":-0.3257279999999998,"y":-9.214144000000003},{"x":6.464479999999998,"y":-15.408736},{"x":15.875456,"y":-3.3769600000000004},{"x":12.122976000000001,"y":2.8176319999999997}]}],"bounds":{"minX":-15.693088000000001,"minY":-15.408736,"maxX":15.875456,"maxY":15.921856000000002}},"parts":3,"score":6},{"collision":{"primary":{"kind":"circle","x":2.0743743999999995,"y":1.294764799999998,"radius":20.3663488},"extras":[{"kind":"polygon","points":[{"x":-10.2125184,"y":-21.155276800000003},{"x":5.381151999999999,"y":-10.3147968},{"x":-18.6347392,"y":-6.8124671999999995},{"x":-15.0490368,"y":-16.5689216}]},{"kind":"polygon","points":[{"x":-4.542092799999999,"y":-7.979910399999999},{"x":14.053580799999999,"y":-21.155276800000003},{"x":19.974169600000003,"y":-15.4014784},{"x":21.8921024,"y":-2.476230400000001}]},{"kind":"polygon","points":[{"x":-18.4679488,"y":-10.4815424},{"x":-5.876281599999999,"y":-8.8137728},{"x":-13.6314304,"y":3.5277312000000016},{"x":-22.3038592,"y":1.4430079999999983}]}],"bounds":{"minX":-22.3038592,"minY":-21.155276800000003,"maxX":22.4407232,"maxY":21.6611136}},"parts":4,"score":10},{"collision":{"primary":{"kind":"circle","x":0.3294208000000026,"y":2.374144000000001,"radius":22.655897600000003},"extras":[{"kind":"polygon","points":[{"x":-15.361792000000001,"y":-13.122099200000001},{"x":-4.280166399999999,"y":-25.2848128},{"x":5.449984000000001,"y":-25.464985600000002},{"x":16.4414976,"y":-13.21216}]},{"kind":"circle","x":-8.888780800000001,"y":20.2735616,"radius":5.7489408},{"kind":"circle","x":9.63712,"y":20.0050688,"radius":5.7150976}],"bounds":{"minX":-22.326476800000002,"minY":-25.464985600000002,"maxX":22.985318400000004,"maxY":26.0225024}},"parts":4,"score":15},{"collision":{"primary":{"kind":"circle","x":-0.5545728000000015,"y":2.369260799999999,"radius":25.188019200000003},"extras":[{"kind":"polygon","points":[{"x":-22.9940352,"y":-28.1646144},{"x":-17.383334400000003,"y":-28.344038400000002},{"x":-2.1799295999999995,"y":-3.8158847999999987},{"x":-23.958950400000003,"y":-8.115609600000003}]},{"kind":"polygon","points":[{"x":0.7593983999999985,"y":-4.0186368},{"x":17.888544,"y":-28.7494848},{"x":22.956364800000003,"y":-28.242720000000006},{"x":24.0728832,"y":-3.505420800000002}]},{"kind":"circle","x":-11.0218176,"y":23.308416,"radius":5.6392128},{"kind":"circle","x":11.225491200000002,"y":23.110272000000002,"radius":5.7856896},{"kind":"polygon","points":[{"x":-17.633318400000004,"y":-28.3790016},{"x":-0.37192319999999945,"y":-22.4822592},{"x":2.8445183999999992,"y":3.8923775999999983},{"x":-10.346572800000002,"y":12.1612608}]},{"kind":"polygon","points":[{"x":-0.15747840000000082,"y":-22.9110912},{"x":17.425612800000003,"y":-28.486252800000003},{"x":10.778342399999998,"y":7.323206400000001},{"x":-3.909945600000002,"y":2.4985728000000016}]}],"bounds":{"minX":-25.742592000000005,"minY":-28.7494848,"maxX":24.6334464,"maxY":28.9476288}},"parts":7,"score":21},{"collision":{"primary":{"kind":"circle","x":-0.29460479999999994,"y":4.291392000000002,"radius":29.3942208},"extras":[{"kind":"polygon","points":[{"x":-27.375532800000002,"y":-29.875372800000005},{"x":-21.817824,"y":-33.18631680000001},{"x":-0.18372480000000097,"y":-24.8533824},{"x":-21.1976352,"y":1.6642080000000026},{"x":-28.9127328,"y":-16.394985600000002}]},{"kind":"polygon","points":[{"x":19.329206400000004,"y":1.6642080000000026},{"x":-0.18372480000000097,"y":-24.728323200000002},{"x":21.224716800000003,"y":-33.4227936},{"x":27.846672,"y":-28.6928544},{"x":28.4378976,"y":-16.5132576}]},{"kind":"circle","x":-12.510959999999999,"y":25.904995200000002,"radius":7.768051200000001},{"kind":"circle","x":12.513715200000004,"y":25.548633600000002,"radius":8.1059328}],"bounds":{"minX":-29.688825599999998,"minY":-33.4227936,"maxX":29.099616,"maxY":33.6856128}},"parts":5,"score":28},{"collision":{"primary":{"kind":"circle","x":0.08870400000000132,"y":3.966643199999996,"radius":33.8668032},"extras":[{"kind":"polygon","points":[{"x":-9.258239999999999,"y":-38.0623872},{"x":8.445312,"y":-37.9272192},{"x":28.040755200000003,"y":-16.3045632},{"x":-26.691456000000002,"y":-16.4397312}]},{"kind":"circle","x":-13.604352,"y":29.339059199999998,"radius":8.8594944},{"kind":"circle","x":12.842035199999998,"y":28.936320000000002,"radius":8.986368}],"bounds":{"minX":-33.7780992,"minY":-38.0623872,"maxX":33.9555072,"maxY":38.1985536}},"parts":4,"score":36},{"collision":{"primary":{"kind":"circle","x":-0.09082880000000274,"y":4.748902399999998,"radius":33.6596992},"extras":[{"kind":"polygon","points":[{"x":-40.2735104,"y":-2.8196864000000006},{"x":-18.650931200000002,"y":-17.234739200000003},{"x":20.2697728,"y":-17.234739200000003},{"x":40.63109120000001,"y":-4.4413952},{"x":51.1826944,"y":7.872819200000004},{"x":0.4490239999999972,"y":17.001062400000002},{"x":-50.2177792,"y":7.872819200000004}]},{"kind":"polygon","points":[{"x":-9.1009024,"y":-37.4158336},{"x":6.215065600000003,"y":-14.3517696},{"x":-19.551846400000002,"y":-15.973376000000002}]},{"kind":"polygon","points":[{"x":-5.857587200000001,"y":-14.892339199999999},{"x":8.557568000000003,"y":-37.4158336},{"x":19.188633600000003,"y":-17.234739200000003}]},{"kind":"circle","x":-13.5153664,"y":30.345113600000005,"radius":7.485030399999999},{"kind":"circle","x":13.154713600000003,"y":29.9871232,"radius":7.736320000000001}],"bounds":{"minX":-50.2177792,"minY":-37.4158336,"maxX":51.1826944,"maxY":38.4086016}},"parts":6,"score":45},{"collision":{"primary":{"kind":"circle","x":-0.7444479999999984,"y":6.0552959999999985,"radius":57.730688},"extras":[{"kind":"circle","x":37.801216000000004,"y":-35.782528000000006,"radius":26.392576},{"kind":"circle","x":-36.46208000000001,"y":-34.675328,"radius":26.294656},{"kind":"circle","x":-24.684928,"y":51.027712,"radius":13.396736000000002},{"kind":"circle","x":24.762240000000006,"y":51.475072000000004,"radius":13.638400000000003}],"bounds":{"minX":-62.756736000000004,"minY":-62.175104000000005,"maxX":64.193792,"maxY":65.113472}},"parts":5,"score":55},{"collision":{"primary":{"kind":"circle","x":-1.6160256000000004,"y":7.548672,"radius":68.3598336},"extras":[{"kind":"polygon","points":[{"x":-39.8688768,"y":-76.6652928},{"x":-25.814169600000003,"y":-73.9623936},{"x":-35.8146048,"y":17.3930496},{"x":-72.843264,"y":-15.851673599999998}]},{"kind":"polygon","points":[{"x":22.025932800000007,"y":-73.1515392},{"x":35.810304,"y":-76.1247744},{"x":69.0551808,"y":-18.554419199999998},{"x":44.72970240000001,"y":32.258611200000004}]},{"kind":"circle","x":-31.15008,"y":62.58938880000001,"radius":15.7358592},{"kind":"circle","x":28.186521599999992,"y":63.1263744,"radius":14.4502272}],"bounds":{"minX":-72.843264,"minY":-76.6652928,"maxX":69.0551808,"maxY":78.32524800000002}},"parts":5,"score":66}]
"""#
}
