/* Original gameplay declarations © HYPERGRYPH. Extracted without semantic rewriting.
See provenance.json for exact source ranges. Native adapter © EndfieldHUD contributors. */

/* Offline native bridge. No DOM, account service, network, sound, or background
   scheduler is exposed to the original simulation declarations below. */
var __now = 1, __timers = [], __nextTimer = 0;
var performance = { now: function() { return __now; } };
var window = {
 addEventListener:function(){}, removeEventListener:function(){},
 setTimeout:function(fn,ms){var id=++__nextTimer;__timers.push({id:id,at:__now+ms,fn:fn});return id;},
 clearTimeout:function(id){__timers=__timers.filter(function(t){return t.id!==id;});}
};
var document = {addEventListener:function(){},removeEventListener:function(){}};
function requestAnimationFrame(){return 0;}
function cancelAnimationFrame(){}
function re(){} // Source sound hook: intentionally silent.
function oT(){} // Source wind image prefetch hook: native renderer owns artwork.
var oC={prewarm:function(){}};
function tC(){return false;}
var eP={enabled:false,revision:0,profiles:null},sR=[];
var sG=Matter;
// Minimal synchronous state container for the unchanged original store reducer.
var x={v:function(factory){var state;function get(){return state;}function set(update){var value=typeof update==='function'?update(state):update;if(value!==state)state=Object.assign({},state,value);}state=factory(set,get);return{getState:get,setState:set};}};
// Exact cubic-Bezier implementation from original vendor module20288 (identity
// dependency2024 is inlined); used by swap and source merge/clear motion.
var s2={A:(function(){var i=(t,e,n)=>(((1-3*n+3*e)*t+(3*n-6*e))*t+3*e)*t;return function(t,e,n,o){return t===e&&n===o?r=>r:r=>0===r||1===r?r:i(function(t,e,n,r,o){var a,s,u=0;do(a=i(s=e+(n-e)/2,r,o)-t)>0?n=s:e=s;while(Math.abs(a)>1e-7&&++u<12);return s;}(r,0,1,t,n),e,o);};})()};
class oH {
 constructor(){this.previewVisible=false;this.dangerCountdownMs=null;this.mergeMotions=new Map;this.ghosts=[];this.nextGhost=-1;this.alpha=1;}
 resize(){} render(_,a){this.alpha=a==null?1:a;} destroy(){this.ghosts=[];this.mergeMotions.clear();}
 clearTutorialDangerDemo(){} resetDropZoneAnimation(){} playDropZoneAnimation(){}
 resetMergeMotions(){this.ghosts=[];this.mergeMotions.clear();}
 setPreview(level,x){this.previewLevel=level;this.previewX=x;}
 movePreview(x){this.previewX=x;} hidePreview(){this.previewVisible=false;}
 showPreview(){this.previewVisible=true;this.previewAppearAt=null;}
 showPreviewWithAppearMotion(delay){this.previewVisible=true;this.previewAppearAt=__now+(delay||0);}
 setDangerCountdown(v){this.dangerCountdownMs=v;} setSwapPath(v){this.swapPath=v;}
 toWorldX(x){return x;} toWorldY(y){return y;}
 playMerge(outgoing,incoming){this.captureGhosts(outgoing,'merge');this.mergeMotions.set(incoming.id,__now);}
 playMergeFadeOut(outgoing){this.captureGhosts(outgoing,'merge');}
 playClearDisappear(body){this.captureGhosts([body],'clear');}
 captureGhosts(bodies,kind){for(var b of bodies){var p=o3(b,b.body),prior=this.mergeMotions.get(b.id),scale=prior===undefined?1:s5(__now-prior).incomingScale;this.mergeMotions.delete(b.id);this.ghosts.push({id:this.nextGhost--,level:b.level,x:p.x,y:p.y,angle:b.body.angle,size:eM(b.level).size,opacity:1,scale:scale,startedAt:__now,kind:kind});}}
}

function X(e) {
        for (var t = 1; t < arguments.length; t++) {
          var r = null != arguments[t] ? arguments[t] : {},
            n = Object.keys(r);
          "function" == typeof Object.getOwnPropertySymbols && (n = n.concat(Object.getOwnPropertySymbols(r).filter(function(e) {
            return Object.getOwnPropertyDescriptor(r, e).enumerable
          }))), n.forEach(function(t) {
            var n;
            n = r[t], t in e ? Object.defineProperty(e, t, {
              value: n,
              enumerable: !0,
              configurable: !0,
              writable: !0
            }) : e[t] = n
          })
        }
        return e
      }

function W(e) {
        return {
          primary: e.primary,
          extras: e.extras,
          bounds: K([e.primary, ...e.extras])
        }
      }

function U(e, t) {
        return "circle" === e.kind ? {
          kind: "circle",
          x: e.x * t,
          y: e.y * t,
          radius: e.radius * t
        } : {
          kind: "polygon",
          points: e.points.map(e => ({
            x: e.x * t,
            y: e.y * t
          }))
        }
      }

function K(e) {
        var t = 1 / 0,
          r = 1 / 0,
          n = -1 / 0,
          i = -1 / 0;
        for (var a of e) {
          if ("circle" === a.kind) {
            t = Math.min(t, a.x - a.radius), r = Math.min(r, a.y - a.radius), n = Math.max(n, a.x + a.radius), i = Math.max(i, a.y + a.radius);
            continue
          }
          for (var s of a.points) t = Math.min(t, s.x), r = Math.min(r, s.y), n = Math.max(n, s.x), i = Math.max(i, s.y)
        }
        return Number.isFinite(t) ? {
          minX: t,
          minY: r,
          maxX: n,
          maxY: i
        } : {
          minX: 0,
          minY: 0,
          maxX: 0,
          maxY: 0
        }
      }

function q(e) {
        return "circle" === e.kind ? {
          x: e.x,
          y: e.y
        } : J(e.points)
      }

function Z(e) {
        if (e.length < 3) return 0;
        for (var t = 0, r = 0; r < e.length; r += 1) {
          var n = e[r],
            i = e[(r + 1) % e.length];
          t += n.x * i.y - i.x * n.y
        }
        return t / 2
      }

function J(e) {
        var t = Z(e);
        if (1e-6 >= Math.abs(t)) {
          if (0 === e.length) return {
            x: 0,
            y: 0
          };
          var r = e.reduce((e, t) => ({
            x: e.x + t.x,
            y: e.y + t.y
          }), {
            x: 0,
            y: 0
          });
          return {
            x: r.x / e.length,
            y: r.y / e.length
          }
        }
        for (var n = 0, i = 0, a = 0; a < e.length; a += 1) {
          var s = e[a],
            o = e[(a + 1) % e.length],
            l = s.x * o.y - o.x * s.y;
          n += (s.x + o.x) * l, i += (s.y + o.y) * l
        }
        var c = 1 / (6 * t);
        return {
          x: n * c,
          y: i * c
        }
      }

function Q(e) {
        var t;
        return "circle" === e.kind ? Number.isFinite((t = e).x) && Number.isFinite(t.y) && Number.isFinite(t.radius) ? t.radius <= 1e-6 ? {
          valid: !1,
          reason: "圆形半径必须大于 0"
        } : {
          valid: !0,
          reason: null
        } : {
          valid: !1,
          reason: "圆形参数必须是有限数值"
        } : function(e) {
          if (e.length < 3) return {
            valid: !1,
            reason: "凸多边形至少需要 3 个点"
          };
          if (e.some(e => !Number.isFinite(e.x) || !Number.isFinite(e.y))) return {
            valid: !1,
            reason: "多边形点位必须是有限数值"
          };
          for (var t = 0; t < e.length; t += 1)
            if (1e-12 >= function(e, t) {
                var r = e.x - t.x,
                  n = e.y - t.y;
                return r * r + n * n
              }(e[t], e[(t + 1) % e.length])) return {
              valid: !1,
              reason: "多边形存在重复点或零长度边"
            };
          if (1e-6 >= Math.abs(Z(e))) return {
            valid: !1,
            reason: "多边形面积过小"
          };
          if (function(e) {
              for (var t = 0; t < e.length; t += 1)
                for (var r = (t + 1) % e.length, n = t + 1; n < e.length; n += 1) {
                  var i = (n + 1) % e.length;
                  if (t !== n && r !== n && i !== t && function(e, t, r, n) {
                      var i = $(e, t, r),
                        a = $(e, t, n),
                        s = $(r, n, e),
                        o = $(r, n, t);
                      return i * a < -1e-6 && s * o < -1e-6
                    }(e[t], e[r], e[n], e[i])) return !0
                }
              return !1
            }(e)) return {
            valid: !1,
            reason: "多边形边不能自相交"
          };
          for (var r = 0, n = 0; n < e.length; n += 1) {
            var i = $(e[n], e[(n + 1) % e.length], e[(n + 2) % e.length]);
            if (1e-6 >= Math.abs(i)) return {
              valid: !1,
              reason: "多边形不能包含共线顶点"
            };
            var a = Math.sign(i);
            if (0 === r) r = a;
            else if (a !== r) return {
              valid: !1,
              reason: "点位必须组成凸多边形"
            }
          }
          return {
            valid: !0,
            reason: null
          }
        }(e.points)
      }

function $(e, t, r) {
        return (t.x - e.x) * (r.y - e.y) - (t.y - e.y) * (r.x - e.x)
      }

function ee(e, t, r) {
        if ("circle" === t) {
          var n;
          return function(e, t, r) {
            if (t <= 0) return 0;
            var n = Math.PI * t * t,
              i = e + t;
            if (i <= r) return 0;
            if (e - t >= r) return n;
            var a = i - r,
              s = t - a;
            return t * t * Math.acos(s / t) - s * Math.sqrt(Math.max(0, 2 * t * a - a * a))
          }(e.position.y, null != (n = e.circleRadius) ? n : 0, r)
        }
        return function(e, t) {
          if (e.length < 3) return 0;
          for (var r = [], n = 0; n < e.length; n += 1) {
            var i = e[n],
              a = e[(n + e.length - 1) % e.length],
              s = i.y >= t;
            if (s !== a.y >= t) {
              var o = i.y - a.y,
                l = 1e-6 >= Math.abs(o) ? 0 : (t - a.y) / o;
              r.push({
                x: a.x + (i.x - a.x) * l,
                y: t
              })
            }
            s && r.push(X({}, i))
          }
          return Math.abs(Z(r))
        }(e.vertices, r)
      }

var ey = {
        maxScore: 99999,
        container: {
          width: 230,
          height: 280
        },
        wallThickness: 80,
        dropY: -10,
        redLineY: 8,
        loseOverflowRatio: .1,
        loseCountdownMs: 5e3,
        danger: {
          lineWidth: 192,
          lineHeight: 3.5,
          overlayHeight: 45,
          countdownHeight: 52
        },
        loseSettleSpeed: .55,
        dropCooldownMs: 500,
        comboWindowMs: 1e3,
        comboMinCount: 2,
        spawnLevelMin: 1,
        spawnLevelMax: 5,
        tuanScale: .8,
        gravityY: 1.05,
        body: {
          restitution: 0,
          friction: 1,
          frictionStatic: 2,
          frictionAir: .001,
          density: .002,
          slop: .042,
          sleepAfterStableMs: 3e3
        },
        wall: {
          restitution: 0,
          friction: .5,
          frictionStatic: 1
        },
        skillPower: {
          ringSlots: 12,
          maxPoints: 3,
          ringActivityMs: 2e3,
          decayIntervalMs: 1e3
        },
        skillPromptDurationMs: 1700,
        goldenPromptDurationMs: 1500,
        wind: {
          fillMs: 1200,
          holdMs: 5e3,
          drainMs: 1400,
          drainBuoyancyMs: 550,
          drainBuoyancyPower: 2.8,
          maxDepthRatio: .75,
          frictionAir: .15,
          buoyancyPerMass: .00155,
          buoyancyLightMul: 1.2,
          buoyancyHeavyMul: .9,
          lightRadius: 50,
          maxSpeed: 20,
          displacementPerArea: 6e-5,
          maxSurfaceLiftPerFrame: 6,
          surfaceWaveAmp: 2,
          surfaceWaveLen: .012,
          rhoScale: 1.32,
          densityToWind: [.5, .6, .7, .8, 1, 1.02, 1.04, 1.06, 1.08, 1.1, 1.12]
        },
        shakeForce: .0095,
        shakeCycles: 3,
        playScale: 1,
        canvasPadX: 75,
        canvasPadTop: 45,
        canvasPadBottom: 20,
        dropZoneHeight: 90,
        dropZone: {
          throwAnimation: {
            dx: 1,
            dy: -45,
            width: 186
          },
          throwMouthAnimation: {
            dx: 1,
            dy: -45,
            width: 186,
            sourceFrame: {
              width: 558,
              height: 419,
              cropX: 188,
              cropY: 228,
              cropWidth: 180,
              cropHeight: 147
            }
          },
          tuanVerticalAlign: "center",
          tuanVerticalOffsetY: {
            top: -22,
            center: 3
          }
        }
      };

var eb = [0xf6a5b0, 0xf7c05a, 0xa3d977, 8308963, 0xb79ce0, 0xf28e6b, 7327936, 0xf4d35e, 0xe0796b, 0x9bb7e8, 0xffcf4d];

var ex = [{
          primary: {
            kind: "circle",
            radius: .413589,
            x: .497004,
            y: .607815
          },
          extras: [{
            kind: "polygon",
            points: [{
              x: .072878,
              y: .258591
            }, {
              x: .292517,
              y: .003586
            }, {
              x: .487959,
              y: .197167
            }, {
              x: .167807,
              y: .379579
            }]
          }, {
            kind: "polygon",
            points: [{
              x: .690847,
              y: .005447
            }, {
              x: .9291,
              y: .271621
            }, {
              x: .923516,
              y: .424252
            }, {
              x: .493543,
              y: .199028
            }]
          }]
        }, {
          primary: {
            kind: "circle",
            radius: .418549,
            x: .497605,
            y: .589744
          },
          extras: [{
            kind: "polygon",
            points: [{
              x: .134168,
              y: .393298
            }, {
              x: .252232,
              y: .030164
            }, {
              x: .495587,
              y: .002637
            }, {
              x: .712145,
              y: .027483
            }, {
              x: .88482,
              y: .411898
            }]
          }]
        }, {
          primary: {
            kind: "circle",
            radius: .396465,
            x: .490777,
            y: .601093
          },
          extras: [{
            kind: "polygon",
            points: [{
              x: .277626,
              y: .024061
            }, {
              x: .493543,
              y: .210196
            }, {
              x: .361387,
              y: .725791
            }, {
              x: .009591,
              y: .388886
            }]
          }, {
            kind: "polygon",
            points: [{
              x: .489821,
              y: .212058
            }, {
              x: .702015,
              y: .018477
            }, {
              x: .996108,
              y: .39447
            }, {
              x: .878843,
              y: .588051
            }]
          }]
        }, {
          primary: {
            kind: "circle",
            radius: .454606,
            x: .546303,
            y: .528901
          },
          extras: [{
            kind: "polygon",
            points: [{
              x: .272042,
              y: .027784
            }, {
              x: .620115,
              y: .269759
            }, {
              x: .084046,
              y: .347936
            }, {
              x: .164084,
              y: .130158
            }]
          }, {
            kind: "polygon",
            points: [{
              x: .398614,
              y: .321877
            }, {
              x: .813696,
              y: .027784
            }, {
              x: .945852,
              y: .156217
            }, {
              x: .988663,
              y: .444727
            }]
          }, {
            kind: "polygon",
            points: [{
              x: .087769,
              y: .266037
            }, {
              x: .368833,
              y: .303264
            }, {
              x: .195727,
              y: .578744
            }, {
              x: .002146,
              y: .53221
            }]
          }]
        }, {
          primary: {
            kind: "circle",
            radius: .442498,
            x: .506434,
            y: .54637
          },
          extras: [{
            kind: "polygon",
            points: [{
              x: .199965,
              y: .243709
            }, {
              x: .416403,
              y: .006156
            }, {
              x: .606445,
              y: .002637
            }, {
              x: .821123,
              y: .24195
            }]
          }, {
            kind: "circle",
            radius: .112284,
            x: .326391,
            y: .895968
          }, {
            kind: "circle",
            radius: .111623,
            x: .688225,
            y: .890724
          }]
        }, {
          primary: {
            kind: "circle",
            radius: .437292,
            x: .490372,
            y: .541133
          },
          extras: [{
            kind: "polygon",
            points: [{
              x: .100798,
              y: .011031
            }, {
              x: .198206,
              y: .007916
            }, {
              x: .462154,
              y: .433752
            }, {
              x: .084046,
              y: .359104
            }]
          }, {
            kind: "polygon",
            points: [{
              x: .513184,
              y: .430232
            }, {
              x: .810565,
              y: 877e-6
            }, {
              x: .898548,
              y: .009675
            }, {
              x: .917932,
              y: .439142
            }]
          }, {
            kind: "circle",
            radius: .097903,
            x: .308649,
            y: .90466
          }, {
            kind: "circle",
            radius: .100446,
            x: .694887,
            y: .90122
          }, {
            kind: "polygon",
            points: [{
              x: .193866,
              y: .007309
            }, {
              x: .493543,
              y: .109683
            }, {
              x: .549384,
              y: .567576
            }, {
              x: .320372,
              y: .711133
            }]
          }, {
            kind: "polygon",
            points: [{
              x: .497266,
              y: .102238
            }, {
              x: .802528,
              y: .005447
            }, {
              x: .687124,
              y: .627139
            }, {
              x: .432119,
              y: .543378
            }]
          }]
        }, {
          primary: {
            kind: "circle",
            radius: .437414,
            x: .495616,
            y: .56386
          },
          extras: [{
            kind: "polygon",
            points: [{
              x: .092626,
              y: .055426
            }, {
              x: .17533,
              y: .006156
            }, {
              x: .497266,
              y: .130158
            }, {
              x: .184559,
              y: .524765
            }, {
              x: .069751,
              y: .256027
            }]
          }, {
            kind: "polygon",
            points: [{
              x: .787637,
              y: .524765
            }, {
              x: .497266,
              y: .132019
            }, {
              x: .815844,
              y: .002637
            }, {
              x: .914385,
              y: .073023
            }, {
              x: .923183,
              y: .254267
            }]
          }, {
            kind: "circle",
            radius: .115596,
            x: .313825,
            y: .885491
          }, {
            kind: "circle",
            radius: .120624,
            x: .686216,
            y: .880188
          }]
        }, {
          primary: {
            kind: "circle",
            radius: .440974,
            x: .501155,
            y: .551649
          },
          extras: [{
            kind: "polygon",
            points: [{
              x: .37945,
              y: .004396
            }, {
              x: .609965,
              y: .006156
            }, {
              x: .865114,
              y: .287701
            }, {
              x: .152455,
              y: .285941
            }]
          }, {
            kind: "circle",
            radius: .115358,
            x: .32286,
            y: .882019
          }, {
            kind: "circle",
            radius: .11701,
            x: .667214,
            y: .876775
          }]
        }, {
          primary: {
            kind: "circle",
            radius: .328708,
            x: .499113,
            y: .546376
          },
          extras: [{
            kind: "polygon",
            points: [{
              x: .106704,
              y: .472464
            }, {
              x: .317862,
              y: .331692
            }, {
              x: .697947,
              y: .331692
            }, {
              x: .896788,
              y: .456627
            }, {
              x: .999831,
              y: .576883
            }, {
              x: .504385,
              y: .666026
            }, {
              x: .009592,
              y: .576883
            }]
          }, {
            kind: "polygon",
            points: [{
              x: .411124,
              y: .134611
            }, {
              x: .560694,
              y: .359846
            }, {
              x: .309064,
              y: .34401
            }]
          }, {
            kind: "polygon",
            points: [{
              x: .442797,
              y: .354567
            }, {
              x: .58357,
              y: .134611
            }, {
              x: .687389,
              y: .331692
            }]
          }, {
            kind: "circle",
            radius: .073096,
            x: .368014,
            y: .796339
          }, {
            kind: "circle",
            radius: .07555,
            x: .628464,
            y: .792843
          }]
        }, {
          primary: {
            kind: "circle",
            radius: .451021,
            x: .494184,
            y: .547307
          },
          extras: [{
            kind: "circle",
            radius: .206192,
            x: .795322,
            y: .220449
          }, {
            kind: "circle",
            radius: .205427,
            x: .21514,
            y: .229099
          }, {
            kind: "circle",
            radius: .104662,
            x: .307149,
            y: .898654
          }, {
            kind: "circle",
            radius: .10655,
            x: .693455,
            y: .902149
          }]
        }, {
          primary: {
            kind: "circle",
            radius: .445051,
            x: .489479,
            y: .549145
          },
          extras: [{
            kind: "polygon",
            points: [{
              x: .240437,
              y: 877e-6
            }, {
              x: .331939,
              y: .018474
            }, {
              x: .266832,
              y: .613236
            }, {
              x: .02576,
              y: .396799
            }]
          }, {
            kind: "polygon",
            points: [{
              x: .643398,
              y: .023753
            }, {
              x: .73314,
              y: .004396
            }, {
              x: .949578,
              y: .379203
            }, {
              x: .791209,
              y: .710017
            }]
          }, {
            kind: "circle",
            radius: .102447,
            x: .2972,
            y: .907483
          }, {
            kind: "circle",
            radius: .094077,
            x: .683506,
            y: .910979
          }]
        }];

var ew = [24, 32, 40, 56, 64, 72, 84, 96, 128, 160, 192];

var eS = [1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 66];

function eO(e) {
        return e.map((e, t) => {
          var r, n = ew[t];
          return {
            level: t + 1,
            size: n,
            collision: W({
              primary: eD(e.primary, n),
              extras: e.extras.map(e => eD(e, n))
            }),
            color: eb[t],
            colorCss: (r = eb[t], "#".concat(r.toString(16).padStart(6, "0"))),
            score: eS[t]
          }
        })
      }

var e_ = eO(ex);

var ek = e_.length;

var eA = NaN;

var ej = -1;

var eE = e_;

function eM(e) {
        var t = Math.min(Math.max(Math.round(e), 1), ek);
        return function() {
          var e, t = ey.tuanScale;
          if (t === eA && eP.revision === ej) return eE;
          var r = eP.enabled && (null == (e = eP.profiles) ? void 0 : e.length) === ex.length ? eP.profiles : null,
            n = r ? eO(r) : e_;
          return eA = t, ej = eP.revision, eE = 1 === t ? n : n.map(e => {
            var r, n, i;
            return n = function(e) {
              for (var t = 1; t < arguments.length; t++) {
                var r = null != arguments[t] ? arguments[t] : {},
                  n = Object.keys(r);
                "function" == typeof Object.getOwnPropertySymbols && (n = n.concat(Object.getOwnPropertySymbols(r).filter(function(e) {
                  return Object.getOwnPropertyDescriptor(r, e).enumerable
                }))), n.forEach(function(t) {
                  var n;
                  n = r[t], t in e ? Object.defineProperty(e, t, {
                    value: n,
                    enumerable: !0,
                    configurable: !0,
                    writable: !0
                  }) : e[t] = n
                })
              }
              return e
            }({}, e), i = i = {
              size: e.size * t,
              collision: (r = e.collision, W({
                primary: U(r.primary, t),
                extras: r.extras.map(e => U(e, t))
              }))
            }, Object.getOwnPropertyDescriptors ? Object.defineProperties(n, Object.getOwnPropertyDescriptors(i)) : (function(e) {
              var t = Object.keys(e);
              if (Object.getOwnPropertySymbols) {
                var r = Object.getOwnPropertySymbols(e);
                t.push.apply(t, r)
              }
              return t
            })(Object(i)).forEach(function(e) {
              Object.defineProperty(n, e, Object.getOwnPropertyDescriptor(i, e))
            }), n
          })
        }()[t - 1]
      }

function eD(e, t) {
        return "circle" === e.kind ? {
          kind: "circle",
          radius: e.radius * t,
          x: (e.x - .5) * t,
          y: (e.y - .5) * t
        } : {
          kind: "polygon",
          points: e.points.map(e => ({
            x: (e.x - .5) * t,
            y: (e.y - .5) * t
          }))
        }
      }

var e0 = [{
          id: "clear",
          cost: 1
        }, {
          id: "wind",
          cost: 2
        }, {
          id: "shake",
          cost: 3
        }, {
          id: "swap",
          cost: 0,
          chargeRequired: 6
        }];

var e1 = e0.reduce((e, t) => (e[t.id] = t, e), {});

var e2 = function(e, t) {
          var r = arguments.length > 2 && void 0 !== arguments[2] ? arguments[2] : ey.skillPower.decayIntervalMs;
          if (e.energyDecayStartedAt <= 0) return e.energyProgress;
          var n = (t - e.energyDecayStartedAt) / r;
          return Math.max(0, e.energyProgress - n)
        };

var e3 = (0, x.v)((e, t) => ({
          state: "idle",
          score: 0,
          mergeCount: 0,
          skillUseCount: 0,
          energy: 0,
          energyProgress: 0,
          lastMergeAt: 0,
          nextEnergyDecayAt: 0,
          energyDecayStartedAt: 0,
          energySpent: 0,
          swapCharge: 0,
          highScore: 0,
          current: 1,
          next: 1,
          unlockedMax: 5,
          maxLevelThisRun: 1,
          skillSession: null,
          newBest: !1,
          guideDone: !1,
          paused: !1,
          start: () => e(e => ({
            state: "playing",
            score: 0,
            mergeCount: 0,
            skillUseCount: 0,
            energy: 0,
            energyProgress: 0,
            lastMergeAt: 0,
            nextEnergyDecayAt: 0,
            energyDecayStartedAt: 0,
            energySpent: 0,
            swapCharge: 0,
            maxLevelThisRun: Math.max(1, e.current, e.next),
            skillSession: null,
            newBest: !1,
            paused: !1
          })),
          addScore: t => e(e => ({
            score: Math.min(ey.maxScore, e.score + t)
          })),
          onMerge: (t, r) => e(e => {
            var n = eM(null != r ? r : t - 1).score,
              i = performance.now(),
              a = e.energy,
              s = e2(e, i);
            return a < ey.skillPower.maxPoints && (s += 1) >= ey.skillPower.ringSlots && (a += 1, s = 0), {
              score: Math.min(ey.maxScore, e.score + n),
              mergeCount: e.mergeCount + 1,
              energy: a,
              energyProgress: s,
              lastMergeAt: i,
              nextEnergyDecayAt: i + ey.skillPower.ringActivityMs,
              energyDecayStartedAt: 0,
              unlockedMax: Math.max(e.unlockedMax, t),
              maxLevelThisRun: Math.max(e.maxLevelThisRun, t)
            }
          }),
          setQueue: (t, r) => e(e => ({
            current: t,
            next: r,
            maxLevelThisRun: Math.max(e.maxLevelThisRun, t, r)
          })),
          advanceQueue: t => e(e => ({
            current: e.next,
            next: t,
            maxLevelThisRun: Math.max(e.maxLevelThisRun, e.next, t)
          })),
          spendEnergy: (r, n) => {
            var i = (null == n ? void 0 : n.deduct) !== !1;
            return (!i || !(t().energy < r)) && (e(e => i ? {
              energy: e.energy - r,
              energyProgress: e2(e, performance.now()),
              nextEnergyDecayAt: 0,
              energyDecayStartedAt: 0,
              energySpent: e.energySpent + r,
              swapCharge: Math.min(6, e.swapCharge + r)
            } : {
              energySpent: e.energySpent + r,
              swapCharge: Math.min(6, e.swapCharge + r)
            }), !0)
          },
          consumeSwapCharge: () => e({
            swapCharge: 0
          }),
          addEnergy: t => e(e => ({
            energy: Math.min(ey.skillPower.maxPoints, Math.max(0, e.energy + t))
          })),
          tickEnergyDecay: t => e(e => {
            let r, n;
            var {
              maxPoints: i,
              ringSlots: a
            } = ey.skillPower;
            if (e.energyDecayStartedAt > 0) return e2(e, t) > 0 ? e : {
              energyProgress: 0,
              nextEnergyDecayAt: 0,
              energyDecayStartedAt: 0
            };
            var s = (r = e.energy, n = e.energyProgress, r === i - 1 ? n : 0);
            return s <= 0 || e.nextEnergyDecayAt <= 0 || t < e.nextEnergyDecayAt ? e : {
              energy: i - 1,
              energyProgress: s,
              nextEnergyDecayAt: 0,
              energyDecayStartedAt: t
            }
          }),
          refreshEnergyDecayTiming: (t, r, n) => e(e => {
            if (e.energyDecayStartedAt > 0) {
              if (!n) return e;
              var i = e2(e, t, r);
              return {
                energyProgress: i,
                energyDecayStartedAt: i > 0 ? t : 0
              }
            }
            return e.nextEnergyDecayAt <= 0 || e.lastMergeAt <= 0 ? e : {
              nextEnergyDecayAt: e.lastMergeAt + ey.skillPower.ringActivityMs
            }
          }),
          setSkillSession: t => e({
            skillSession: t
          }),
          incSkillUse: () => e(e => ({
            skillUseCount: e.skillUseCount + 1
          })),
          setState: t => e({
            state: t
          }),
          commitHighScore: () => {
            var {
              score: r,
              highScore: n
            } = t(), i = r > n;
            return i ? e({
              highScore: r,
              newBest: !0
            }) : e({
              newBest: !1
            }), i
          },
          hydrateProfile: (t, r, n) => e(e => ({
            highScore: Math.max(e.highScore, t || 0),
            unlockedMax: Math.max(e.unlockedMax, 5, r || 5),
            guideDone: e.guideDone || n
          })),
          setPaused: t => e({
            paused: t
          })
        }));

var e5 = () => e3.getState();

var aZ = [0, 0, 0, 1];

var aJ = [.5, 0, 0, 1];

var a$ = [1, 0, 1, 1];

function sL(e, t, r) {
        return t in e ? Object.defineProperty(e, t, {
          value: r,
          enumerable: !0,
          configurable: !0,
          writable: !0
        }) : e[t] = r, e
      }

function sI(e) {
        if (e < 0) return null;
        var t = Math.floor(e / 1100);
        return t < 0 || t > 3 ? null : e - 1100 * t < 1e3 ? 3 - t : null
      }

class sF {
        isTracking() {
          return null !== this.endAt || null !== this.violateSince
        }
        pause(e) {
          null === this.pausedAt && (this.pausedAt = e)
        }
        resume(e) {
          if (null !== this.pausedAt) {
            var t = e - this.pausedAt;
            this.pausedAt = null, null !== this.endAt && (this.endAt += t), null !== this.violateSince && (this.violateSince += t), null !== this.clearSince && (this.clearSince += t)
          }
        }
        update(e, t, r) {
          if (null === this.pausedAt) {
            if (!e) {
              if (this.violateSince = null, null === this.endAt || (null === this.clearSince && (this.clearSince = t), t - this.clearSince < 220)) return;
              return this.reset(), null
            }
            return (this.clearSince = null, null !== this.endAt) ? this.endAt - t : (null === this.violateSince && (this.violateSince = t), t - this.violateSince < 120) ? void 0 : (this.endAt = t + r, this.endAt - t)
          }
        }
        reset() {
          this.endAt = null, this.violateSince = null, this.clearSince = null, this.pausedAt = null
        }
        constructor() {
          sL(this, "endAt", null), sL(this, "violateSince", null), sL(this, "clearSince", null), sL(this, "pausedAt", null)
        }
      }

function sV(e, t) {
        var r, n = arguments.length > 2 && void 0 !== arguments[2] ? arguments[2] : (r = ey.dropZone.tuanVerticalAlign, {
            dropY: ey.dropY,
            verticalAlign: r,
            verticalOffsetY: ey.dropZone.tuanVerticalOffsetY[r]
          }),
          i = q(e.primary),
          a = "top" === n.verticalAlign ? e.bounds.minY : (e.bounds.minY + e.bounds.maxY) / 2;
        return {
          x: t - i.x,
          y: n.dropY + n.verticalOffsetY - a
        }
      }

var sz = 1e3 / 60;

class sY {
        get interpolationAlpha() {
          return Math.max(0, Math.min(1, this.accumulatorMs / sz))
        }
        takeSteps(e) {
          if (!Number.isFinite(e) || e <= 0) return 0;
          this.accumulatorMs += Math.min(e, 50);
          var t = Math.min(Math.floor((this.accumulatorMs + 1e-9) / sz), 3);
          return this.accumulatorMs -= t * sz, 3 === t && this.accumulatorMs >= sz && (this.accumulatorMs %= sz), t
        }
        reset() {
          this.accumulatorMs = 0
        }
        constructor() {
          ! function(e, t) {
            t in e ? Object.defineProperty(e, t, {
              value: 0,
              enumerable: !0,
              configurable: !0,
              writable: !0
            }) : e[t] = 0
          }(this, "accumulatorMs")
        }
      }

function sH(e, t, r) {
        return t in e ? Object.defineProperty(e, t, {
          value: r,
          enumerable: !0,
          configurable: !0,
          writable: !0
        }) : e[t] = r, e
      }

class sX {
        takeElapsed(e) {
          if (e + .5 < this.nextTickAt) return null;
          var t = Math.max(0, e - this.lastTickAt);
          this.lastTickAt = e;
          var r = Math.max(1, Math.floor((e - this.nextTickAt) / this.intervalMs) + 1);
          return this.nextTickAt += r * this.intervalMs, t
        }
        reset(e) {
          this.lastTickAt = e, this.nextTickAt = e + this.intervalMs
        }
        constructor(e, t) {
          sH(this, "intervalMs", void 0), sH(this, "lastTickAt", void 0), sH(this, "nextTickAt", void 0), this.intervalMs = 1e3 / e, this.lastTickAt = t, this.nextTickAt = t + this.intervalMs
        }
      }

function sW(e, t) {
        return e >= 0 && e <= ey.container.width && t >= 0 && t <= ey.container.height
      }

function sU(e, t, r) {
        return t in e ? Object.defineProperty(e, t, {
          value: r,
          enumerable: !0,
          configurable: !0,
          writable: !0
        }) : e[t] = r, e
      }

function sK(e) {
        for (var t = 1; t < arguments.length; t++) {
          var r = null != arguments[t] ? arguments[t] : {},
            n = Object.keys(r);
          "function" == typeof Object.getOwnPropertySymbols && (n = n.concat(Object.getOwnPropertySymbols(r).filter(function(e) {
            return Object.getOwnPropertyDescriptor(r, e).enumerable
          }))), n.forEach(function(t) {
            sU(e, t, r[t])
          })
        }
        return e
      }

class sq {
        addWalls() {
          var e = ey.wallThickness,
            t = sK({
              isStatic: !0
            }, ey.wall);
          this.walls.push(sG.Bodies.rectangle(-e / 2, this.h / 2, e, 3 * this.h, t), sG.Bodies.rectangle(this.w + e / 2, this.h / 2, e, 3 * this.h, t), sG.Bodies.rectangle(this.w / 2, this.h + e / 2, this.w + 2 * e, e, t)), sG.Composite.add(this.engine.world, this.walls)
        }
        syncBodyProps() {
          var {
            restitution: e,
            friction: t,
            frictionStatic: r,
            frictionAir: n,
            slop: i,
            sleepAfterStableMs: a
          } = ey.body;
          this.bodies.forEach(s => {
            var {
              body: o
            } = s;
            o.restitution = e, o.friction = t, o.frictionStatic = r, o.frictionAir = n, o.slop = i, o.sleepThreshold = s1(a), sJ(o, s.collisionParts[0].body)
          })
        }
        syncWallProps() {
          var {
            restitution: e,
            friction: t,
            frictionStatic: r
          } = ey.wall;
          for (var n of this.walls) n.restitution = e, n.friction = t, n.frictionStatic = r
        }
        spawn(e, t, r, n) {
          var i, a, s, o = [(i = eM(e).collision).primary, ...i.extras].map(e => ({
              body: function(e, t, r) {
                var n, i = Q(e);
                if (!i.valid) throw Error("Invalid ".concat(e.kind, " collision shape: ").concat(i.reason));
                if ("circle" === e.kind) return sG.Bodies.circle(t + e.x, r + e.y, e.radius, {
                  label: "tuan-part"
                });
                var a = Z(n = e.points.map(e => X({}, e))) >= 0 ? n : n.reverse(),
                  s = J(a);
                return sG.Body.create({
                  position: {
                    x: t + s.x,
                    y: r + s.y
                  },
                  vertices: a,
                  label: "tuan-part"
                })
              }(e, t, r),
              kind: e.kind
            })),
            l = sG.Body.create({
              parts: o.map(e => e.body),
              restitution: ey.body.restitution,
              friction: ey.body.friction,
              frictionStatic: ey.body.frictionStatic,
              frictionAir: ey.body.frictionAir,
              density: ey.body.density,
              slop: ey.body.slop,
              sleepThreshold: s1(ey.body.sleepAfterStableMs),
              label: "tuan"
            });
          a = sK({}, l.plugin), s = s = {
            level: e
          }, Object.getOwnPropertyDescriptors ? Object.defineProperties(a, Object.getOwnPropertyDescriptors(s)) : (function(e) {
            var t = Object.keys(e);
            if (Object.getOwnPropertySymbols) {
              var r = Object.getOwnPropertySymbols(e);
              t.push.apply(t, r)
            }
            return t
          })(Object(s)).forEach(function(e) {
            Object.defineProperty(a, e, Object.getOwnPropertyDescriptor(s, e))
          }), l.plugin = a, sJ(l, o[0].body), (null == n ? void 0 : n.velocity) && sG.Body.setVelocity(l, n.velocity), (null == n ? void 0 : n.angularVelocity) != null && sG.Body.setAngularVelocity(l, n.angularVelocity);
          var c = {
            body: l,
            level: e,
            id: l.id,
            collisionParts: o,
            renderOffset: {
              x: t - l.position.x,
              y: r - l.position.y
            },
            previousTransform: s$(l),
            renderTransform: s$(l)
          };
          return this.bodies.set(l.id, c), sG.Composite.add(this.engine.world, l), c
        }
        remove(e) {
          var t = !(arguments.length > 1) || void 0 === arguments[1] || arguments[1],
            r = this.bodies.get(e);
          r && (sG.Composite.remove(this.engine.world, r.body), this.suspendedIds.delete(e), this.bodies.delete(e), t && this.wakeAll())
        }
        getBody(e) {
          return this.bodies.get(e)
        }
        scaleTuans(e) {
          1 !== e && this.bodies.forEach(t => {
            sG.Sleeping.set(t.body, !1);
            var {
              body: r,
              renderOffset: n
            } = t, i = Math.cos(r.angle), a = Math.sin(r.angle), s = r.position.x + n.x * i - n.y * a, o = r.position.y + n.x * a + n.y * i;
            for (var l of (sG.Body.scale(r, e, e), sJ(r, t.collisionParts[0].body), t.collisionParts)) "circle" === l.kind && l.body.circleRadius && (l.body.circleRadius *= e);
            n.x *= e, n.y *= e;
            var c = n.x * i - n.y * a,
              u = n.x * a + n.y * i;
            sG.Body.setPosition(r, {
              x: s - c,
              y: o - u
            }), s0(t)
          })
        }
        pickBodyAt(e, t, r) {
          for (var n of Array.from(this.bodies.values()).reverse())
            if (!this.suspendedIds.has(n.id) && (!r || r(n)) && sG.Query.point([n.body], {
                x: e,
                y: t
              }).length > 0) return n;
          return null
        }
        captureKinematics(e) {
          var t = this.bodies.get(e);
          return t ? {
            position: {
              x: t.body.position.x,
              y: t.body.position.y
            },
            angle: t.body.angle,
            velocity: {
              x: t.body.velocity.x,
              y: t.body.velocity.y
            },
            angularVelocity: t.body.angularVelocity
          } : null
        }
        suspend(e) {
          var t = !1;
          for (var r of e) {
            var n = this.bodies.get(r);
            !n || this.suspendedIds.has(r) || (sG.Composite.remove(this.engine.world, n.body), this.suspendedIds.add(r), t = !0)
          }
          t && this.wakeAll()
        }
        resume(e) {
          for (var t of e) {
            var r = this.bodies.get(t);
            r && this.suspendedIds.delete(t) && (sG.Sleeping.set(r.body, !1), sG.Composite.add(this.engine.world, r.body))
          }
        }
        setKinematics(e, t) {
          var r = !(arguments.length > 2) || void 0 === arguments[2] || arguments[2],
            n = this.bodies.get(e);
          n && (sG.Sleeping.set(n.body, !1), sG.Body.setPosition(n.body, t.position), sG.Body.setAngle(n.body, t.angle), sG.Body.setVelocity(n.body, t.velocity), sG.Body.setAngularVelocity(n.body, t.angularVelocity), r && s0(n))
        }
        isSuspended(e) {
          return this.suspendedIds.has(e)
        }
        wakeBody(e) {
          var t = this.bodies.get(e);
          t && sG.Sleeping.set(t.body, !1)
        }
        wakeAll() {
          this.bodies.forEach(e => {
            this.suspendedIds.has(e.id) || sG.Sleeping.set(e.body, !1)
          })
        }
        setSleepingEnabled(e) {
          var t = this.engine.enableSleeping;
          return this.engine.enableSleeping = e, e || this.wakeAll(), t
        }
        capturePreviousTransforms() {
          this.bodies.forEach(s0)
        }
        enableSkillCeiling() {
          if (!this.skillCeiling) {
            var e = ey.wallThickness,
              t = ey.redLineY;
            this.skillCeiling = sG.Bodies.rectangle(this.w / 2, t - e / 2, this.w + 2 * e, e, sK({
              isStatic: !0,
              label: "skill-ceiling"
            }, ey.wall)), sG.Composite.add(this.engine.world, this.skillCeiling)
          }
        }
        disableSkillCeiling() {
          this.skillCeiling && (sG.Composite.remove(this.engine.world, this.skillCeiling), this.skillCeiling = null)
        }
        applyShake(e) {
          var t = Math.min(1, Math.max(-1, e)) * ey.shakeForce;
          this.bodies.forEach(e => {
            this.suspendedIds.has(e.id) || (0 !== t && sG.Sleeping.set(e.body, !1), e.body.force.x += t * e.body.mass)
          })
        }
        update() {
          sG.Engine.update(this.engine, sz)
        }
        clearBodies() {
          this.bodies.forEach(e => {
            sG.Composite.remove(this.engine.world, e.body)
          }), this.bodies.clear(), this.suspendedIds.clear()
        }
        destroy() {
          this.disableSkillCeiling(), this.clearBodies(), sG.Engine.clear(this.engine)
        }
        constructor() {
          sU(this, "engine", void 0), sU(this, "bodies", new Map), sU(this, "suspendedIds", new Set), sU(this, "walls", []), sU(this, "skillCeiling", null), sU(this, "w", ey.container.width), sU(this, "h", ey.container.height), this.engine = sG.Engine.create({
            positionIterations: 12,
            velocityIterations: 7,
            constraintIterations: 3,
            enableSleeping: !0
          }), this.engine.gravity.y = ey.gravityY, this.addWalls()
        }
      }

function sZ(e) {
        var t;
        return null != (t = e.parent) ? t : e
      }

function sJ(e, t) {
        sG.Body.setDensity(t, ey.body.density), sG.Body.setCentre(e, t.position), sG.Body.setMass(e, t.mass), sG.Body.setInertia(e, t.inertia)
      }

function sQ(e, t) {
        var r, n, i, a = Math.max(0, Math.min(1, t)),
          s = e.body,
          o = e.previousTransform,
          l = e.renderTransform,
          c = (r = o.angle, n = s.angle, ((n - r + Math.PI) % (i = 2 * Math.PI) + i) % i - Math.PI);
        return l.position.x = o.position.x + (s.position.x - o.position.x) * a, l.position.y = o.position.y + (s.position.y - o.position.y) * a, l.angle = o.angle + c * a, l
      }

function s$(e) {
        return {
          position: {
            x: e.position.x,
            y: e.position.y
          },
          angle: e.angle
        }
      }

function s0(e) {
        var {
          body: t,
          previousTransform: r
        } = e;
        r.position.x = t.position.x, r.position.y = t.position.y, r.angle = t.angle
      }

function s1(e) {
        return Math.max(0, Math.ceil(e / sz))
      }

var s3 = (0, s2.A)(...aZ);

function s5(e) {
        var t = arguments.length > 1 && void 0 !== arguments[1] && arguments[1];
        if (t) return {
          incomingScale: 1,
          outgoingOpacity: 0,
          complete: !0
        };
        var r = Math.max(0, e),
          n = s3(Math.min(1, r / 300)),
          i = r - 16;
        return {
          incomingScale: .8 + .19999999999999996 * n,
          outgoingOpacity: i <= 0 ? 1 : Math.max(0, 1 - i / 50),
          complete: r >= 300
        }
      }

var om = (0, s2.A)(...aJ);

var op = (0, s2.A)(...a$);

function of(e) {
        for (var t = 1; t < arguments.length; t++) {
          var r = null != arguments[t] ? arguments[t] : {},
            n = Object.keys(r);
          "function" == typeof Object.getOwnPropertySymbols && (n = n.concat(Object.getOwnPropertySymbols(r).filter(function(e) {
            return Object.getOwnPropertyDescriptor(r, e).enumerable
          }))), n.forEach(function(t) {
            var n;
            n = r[t], t in e ? Object.defineProperty(e, t, {
              value: n,
              enumerable: !0,
              configurable: !0,
              writable: !0
            }) : e[t] = n
          })
        }
        return e
      }

var ov = 80 * Math.PI / 180;

var oy = (0, s2.A)(...aJ);

function ob(e) {
        return Math.min(1, Math.max(0, e))
      }

function ox(e, t, r) {
        var n = 1 - t;
        return r.x = n * n * e.start.x + 2 * n * t * e.control.x + t * t * e.end.x, r.y = n * n * e.start.y + 2 * n * t * e.control.y + t * t * e.end.y, r
      }

function ow(e) {
        var t = Math.max(0, e),
          r = oy(ob(t / 400)),
          n = ob((t - 500) / 800);
        return {
          arrowOpacity: r * (1 - ob((t - 1100) / 200)),
          arrowScale: r,
          arrowSpinOffset: ov * (1 - r),
          moveProgress: oy(n),
          moving: t >= 500,
          complete: t >= 1300
        }
      }

function oS(e) {
        return Math.min(138, Math.max(44, 3 * (eM(e).size / 2)))
      }

function oO(e, t, r, n) {
        return sW(r, n) ? e.pickBodyAt(r, n, "clear" === t ? e => (function(e, t, r) {
          var {
            body: n,
            renderOffset: i
          } = e, a = Math.cos(n.angle), s = Math.sin(n.angle), o = n.position.x + i.x * a - i.y * s, l = n.position.y + i.x * s + i.y * a, c = .975 * oS(e.level) / 2;
          return Math.abs(t - o) <= c && Math.abs(r - l) <= c
        })(e, r, n) : void 0) : null
      }

function oW(e, t) {
        var r = t.body.angle - e.body.angle,
          n = Math.cos(r),
          i = Math.sin(r),
          a = t => {
            var r = t.x - e.body.position.x,
              a = t.y - e.body.position.y;
            return {
              x: r * n - a * i,
              y: r * i + a * n
            }
          },
          s = K(e.collisionParts.map(e => {
            var t, r, {
              body: n,
              kind: i
            } = e;
            return "circle" === i && n.circleRadius ? (t = function(e) {
              for (var t = 1; t < arguments.length; t++) {
                var r = null != arguments[t] ? arguments[t] : {},
                  n = Object.keys(r);
                "function" == typeof Object.getOwnPropertySymbols && (n = n.concat(Object.getOwnPropertySymbols(r).filter(function(e) {
                  return Object.getOwnPropertyDescriptor(r, e).enumerable
                }))), n.forEach(function(t) {
                  var n;
                  n = r[t], t in e ? Object.defineProperty(e, t, {
                    value: n,
                    enumerable: !0,
                    configurable: !0,
                    writable: !0
                  }) : e[t] = n
                })
              }
              return e
            }({
              kind: "circle"
            }, a(n.position)), r = r = {
              radius: n.circleRadius
            }, Object.getOwnPropertyDescriptors ? Object.defineProperties(t, Object.getOwnPropertyDescriptors(r)) : (function(e) {
              var t = Object.keys(e);
              if (Object.getOwnPropertySymbols) {
                var r = Object.getOwnPropertySymbols(e);
                t.push.apply(t, r)
              }
              return t
            })(Object(r)).forEach(function(e) {
              Object.defineProperty(t, e, Object.getOwnPropertyDescriptor(r, e))
            }), t) : {
              kind: "polygon",
              points: n.vertices.map(a)
            }
          })),
          {
            width: o,
            height: l
          } = ey.container,
          c = .5 - s.minX,
          u = o - .5 - s.maxX,
          d = .5 - s.minY,
          h = l - .5 - s.maxY;
        return {
          x: c <= u ? Math.max(c, Math.min(u, t.body.position.x)) : (c + u) / 2,
          y: Math.min(h, Math.max(d, t.body.position.y))
        }
      }

function oU(e, t, r) {
        return t in e ? Object.defineProperty(e, t, {
          value: r,
          enumerable: !0,
          configurable: !0,
          writable: !0
        }) : e[t] = r, e
      }

class oK {
        start(e) {
          var t = ey.wind,
            r = ey.container.height,
            n = (ey.container.height - ey.redLineY) * t.maxDepthRatio;
          this.savedSleepingEnabled = this.phys.setSleepingEnabled(!1), this.phys.enableSkillCeiling(), this.applyWindFriction(), this.state = {
            phase: "fill",
            start: e,
            fillMs: t.fillMs,
            holdMs: t.holdMs,
            drainMs: t.drainMs,
            bottom: r,
            maxDepth: n,
            surfaceY: r,
            baseSurfaceY: r,
            buoyancyStrength: 1,
            drainFrictionBlend: 0,
            drainBaseFrom: r
          }
        }
        static totalDurationMs() {
          var {
            fillMs: e,
            holdMs: t,
            drainMs: r
          } = ey.wind;
          return e + t + r
        }
        isActive() {
          return null != this.state
        }
        tick(e) {
          return !!this.state && (this.tickPhase(e), !!this.state && (this.applyPhysics(e), this.capVelocities(), null != this.state))
        }
        end(e) {
          this.state && (this.restoreFriction(), this.phys.disableSkillCeiling(), e && this.nudgeAfterDrain(), this.state = null, null !== this.savedSleepingEnabled && (this.phys.setSleepingEnabled(this.savedSleepingEnabled), this.savedSleepingEnabled = null))
        }
        destroy() {
          this.state && this.end(!1)
        }
        tickPhase(e) {
          var t = this.state;
          if (t) {
            var r = ey.wind;
            t.fillMs = Math.max(1, r.fillMs), t.holdMs = Math.max(0, r.holdMs), t.drainMs = Math.max(1, r.drainMs), t.maxDepth = (ey.container.height - ey.redLineY) * r.maxDepthRatio;
            var n = e - t.start;
            if ("fill" === t.phase) {
              var i = Math.min(1, n / t.fillMs);
              t.baseSurfaceY = t.bottom - t.maxDepth * i, this.applyDisplacement(t), i >= 1 && (t.phase = "hold", t.start = e);
              return
            }
            if ("hold" === t.phase) {
              t.baseSurfaceY = t.bottom - t.maxDepth, this.applyDisplacement(t), n >= t.holdMs && (t.phase = "drain", t.start = e, t.drainBaseFrom = t.baseSurfaceY);
              return
            }
            if ("drain" === t.phase) {
              var a = Math.min(1, n / t.drainMs),
                s = Math.min(1, n / Math.max(1, r.drainBuoyancyMs));
              t.buoyancyStrength = Math.pow(1 - s, r.drainBuoyancyPower), t.drainFrictionBlend = s, t.baseSurfaceY = t.drainBaseFrom + (t.bottom - t.drainBaseFrom) * a, this.applyDisplacement(t), a >= 1 && this.end(!0)
            }
          }
        }
        applyDisplacement(e) {
          var t = ey.wind,
            r = ey.redLineY + 16,
            n = e.baseSurfaceY,
            i = 0;
          this.phys.bodies.forEach(e => {
            if (!this.phys.isSuspended(e.id)) {
              var t = e.collisionParts[0];
              t && (i += ee(t.body, t.kind, n))
            }
          });
          var a = Math.max(r, n - Math.min(i * t.displacementPerArea, t.maxSurfaceLiftPerFrame));
          e.surfaceY = e.surfaceY + (a - e.surfaceY) * .35
        }
        applyPhysics(e) {
          var t = this.state;
          if (t && !(t.surfaceY >= t.bottom - 2)) {
            var r = ey.wind,
              n = t.buoyancyStrength,
              i = r.frictionAir,
              a = t.drainFrictionBlend;
            this.phys.bodies.forEach(s => {
              if (this.phys.isSuspended(s.id)) return;
              var o, l, c, u, d, h = s.body;
              this.savedFrictionAir.has(h) || this.savedFrictionAir.set(h, h.frictionAir);
              var m = null != (d = this.savedFrictionAir.get(h)) ? d : ey.body.frictionAir,
                p = "drain" === t.phase ? i * (1 - a) + m * a : i;
              sG.Body.set(h, {
                frictionAir: p
              });
              var g = s.collisionParts[0];
              if (g) {
                var f = (o = g.body, "circle" === g.kind && o.circleRadius ? Math.PI * o.circleRadius * o.circleRadius : o.area);
                if (!(f <= 0)) {
                  var v = Math.sqrt(f / Math.PI),
                    y = (l = t.surfaceY, c = g.body.position.x, u = r.surfaceWaveAmp, l + Math.sin(c * r.surfaceWaveLen + .004 * e) * u),
                    b = ee(g.body, g.kind, y);
                  if (!(b <= 0) && !(n <= 0)) {
                    var x = Math.min(1, b / f),
                      w = function(e) {
                        var t, {
                            densityToWind: r,
                            rhoScale: n
                          } = ey.wind,
                          i = Math.min(Math.max(e - 1, 0), r.length - 1);
                        return (null != (t = r[i]) ? t : 1) / (n > .5 ? n : 1)
                      }(s.level),
                      S = r.buoyancyPerMass * h.mass * x * (1 / w);
                    v < r.lightRadius || w < 1 ? S *= r.buoyancyLightMul : w > 1.02 && (S *= r.buoyancyHeavyMul), S *= n, sG.Body.applyForce(h, h.position, {
                      x: 0,
                      y: -S
                    })
                  }
                }
              }
            })
          }
        }
        capVelocities() {
          var e = ey.wind.maxSpeed;
          this.phys.bodies.forEach(t => {
            if (!this.phys.isSuspended(t.id)) {
              var r = t.body,
                n = r.velocity.x,
                i = r.velocity.y;
              i > e ? i = e : i < -e && (i = -e);
              var a = Math.sqrt(n * n + i * i);
              if (a > e) {
                var s = e / a;
                n *= s, i *= s
              }(n !== r.velocity.x || i !== r.velocity.y) && sG.Body.setVelocity(r, {
                x: n,
                y: i
              })
            }
          })
        }
        applyWindFriction() {
          var e = ey.wind.frictionAir;
          this.phys.bodies.forEach(t => {
            if (!this.phys.isSuspended(t.id)) {
              var r = t.body;
              this.savedFrictionAir.has(r) || this.savedFrictionAir.set(r, r.frictionAir), sG.Body.set(r, {
                frictionAir: e
              })
            }
          })
        }
        restoreFriction() {
          this.phys.bodies.forEach(e => {
            var t = e.body,
              r = this.savedFrictionAir.get(t);
            void 0 !== r && (sG.Body.set(t, {
              frictionAir: r
            }), this.savedFrictionAir.delete(t))
          })
        }
        nudgeAfterDrain() {
          this.phys.wakeAll(), this.phys.bodies.forEach(e => {
            if (!this.phys.isSuspended(e.id)) {
              var t = e.body;
              sG.Body.setPosition(t, {
                x: t.position.x + (Math.random() - .5) * 3,
                y: t.position.y + (Math.random() - .5) * 2
              }), sG.Body.setVelocity(t, {
                x: t.velocity.x + (Math.random() - .5) * .4,
                y: t.velocity.y + .3 * Math.random()
              })
            }
          })
        }
        constructor(e) {
          oU(this, "phys", void 0), oU(this, "state", void 0), oU(this, "savedSleepingEnabled", void 0), oU(this, "savedFrictionAir", void 0), this.phys = e, this.state = null, this.savedSleepingEnabled = null, this.savedFrictionAir = new WeakMap
        }
      }

function oq(e, t, r) {
        return t in e ? Object.defineProperty(e, t, {
          value: r,
          enumerable: !0,
          configurable: !0,
          writable: !0
        }) : e[t] = r, e
      }

function oZ(e) {
        for (var t = 1; t < arguments.length; t++) {
          var r = null != arguments[t] ? arguments[t] : {},
            n = Object.keys(r);
          "function" == typeof Object.getOwnPropertySymbols && (n = n.concat(Object.getOwnPropertySymbols(r).filter(function(e) {
            return Object.getOwnPropertyDescriptor(r, e).enumerable
          }))), n.forEach(function(t) {
            oq(e, t, r[t])
          })
        }
        return e
      }

function oJ(e, t) {
        return t = null != t ? t : {}, Object.getOwnPropertyDescriptors ? Object.defineProperties(e, Object.getOwnPropertyDescriptors(t)) : (function(e) {
          var t = Object.keys(e);
          if (Object.getOwnPropertySymbols) {
            var r = Object.getOwnPropertySymbols(e);
            t.push.apply(t, r)
          }
          return t
        })(Object(t)).forEach(function(r) {
          Object.defineProperty(e, r, Object.getOwnPropertyDescriptor(t, r))
        }), e
      }

var oQ = {
        clear: 650,
        shake: 2e3,
        swap: 1300
      };

function o$(e) {
        return {
          position: {
            x: e.position.x,
            y: e.position.y
          },
          angle: e.angle,
          velocity: {
            x: 0,
            y: 0
          },
          angularVelocity: 0
        }
      }

class o0 {
        activate(e) {
          var t, r, n = e5();
          if ("playing" !== n.state || n.paused || (null == (t = n.skillSession) ? void 0 : t.phase) === "casting") return !1;
          if ((null == (r = n.skillSession) ? void 0 : r.id) === e) return this.cancel(), !0;
          var i = e1[e];
          if (null != i.chargeRequired ? n.swapCharge < i.chargeRequired : n.energy < i.cost) return !1;
          this.cancel(!1);
          var a = performance.now();
          return n.setSkillSession({
            id: e,
            phase: "clear" === e || "swap" === e ? "selecting" : "armed",
            hoverBodyId: null,
            selectedBodyIds: [],
            startedAt: a,
            endsAt: null
          }), "wind" === e && oC.prewarm(), re("select_skill"), this.renderer.hidePreview(), !0
        }
        pointerMove(e, t) {
          var r, n, i = e5().skillSession;
          if (!i || "casting" === i.phase) return !1;
          if ("selecting" !== i.phase) return !0;
          var a = null != (r = null == (n = oO(this.phys, i.id, e, t)) ? void 0 : n.id) ? r : null;
          return a !== i.hoverBodyId && (e5().setSkillSession(oJ(oZ({}, i), {
            hoverBodyId: a
          })), "clear" === i.id && null !== a && re("skill_hover")), !0
        }
        pointerUp(e, t) {
          var r = e5().skillSession;
          if (!r) return !1;
          if ("casting" === r.phase) return !0;
          if (!sW(e, t)) return this.cancel(), !0;
          if ("wind" === r.id || "shake" === r.id) return this.startInstantCast(r.id), !0;
          var n = oO(this.phys, r.id, e, t);
          return !n || (this.confirmTarget(r, n), !0)
        }
        confirm() {
          var e = e5().skillSession;
          if (!e) return !1;
          if ("casting" === e.phase) return !0;
          if ("wind" === e.id || "shake" === e.id) return this.startInstantCast(e.id), !0;
          if (null == e.hoverBodyId) return !0;
          var t = this.phys.bodies.get(e.hoverBodyId);
          return !t || (this.confirmTarget(e, t), !0)
        }
        confirmTarget(e, t) {
          if ("clear" === e.id) {
            if (!this.settleSkill("clear")) return;
            re("clear_tuan"), this.renderer.playClearDisappear(t), this.phys.remove(t.id), this.enterCasting("clear", [], oQ.clear);
            return
          }
          re("select_tuan");
          var r = e.selectedBodyIds[0];
          null == r ? e5().setSkillSession(oJ(oZ({}, e), {
            hoverBodyId: null,
            selectedBodyIds: [t.id]
          })) : r === t.id ? e5().setSkillSession(oJ(oZ({}, e), {
            hoverBodyId: null,
            selectedBodyIds: []
          })) : this.startSwap(r, t.id)
        }
        tick(e) {
          var t = e5().skillSession;
          if (t && "casting" === t.phase) {
            if ("wind" === t.id && this.windRuntime && !this.windRuntime.tick(e)) {
              this.windRuntime = null, this.finish();
              return
            }
            var r, n = e - t.startedAt;
            "shake" === t.id && n < 1200 && this.phys.applyShake(Math.sin(Math.PI * (r = Math.min(1, Math.max(0, n / 1200)))) * Math.sin(2 * Math.PI * Math.max(1, Math.round(ey.shakeCycles)) * r)), "swap" === t.id && this.tickSwap(e), "wind" !== t.id && null != t.endsAt && e >= t.endsAt && this.finish()
          }
        }
        isActive() {
          return null != e5().skillSession
        }
        isPreparing() {
          var e = e5().skillSession;
          return null != e && "casting" !== e.phase
        }
        cancel() {
          var e = !(arguments.length > 0) || void 0 === arguments[0] || arguments[0];
          if (this.swapRuntime && !this.swapRuntime.completed) {
            var {
              ids: t,
              left: r,
              right: n
            } = this.swapRuntime;
            this.phys.setKinematics(t[0], r), this.phys.setKinematics(t[1], n), this.phys.resume(t)
          }
          this.swapRuntime = null, this.windRuntime && (this.windRuntime.end(!1), this.windRuntime = null), oT(), this.endShake(), this.renderer.setSwapPath(null), e5().setSkillSession(null), e && this.restorePreview()
        }
        destroy() {
          this.cancel(!1)
        }
        startInstantCast(e) {
          if (this.settleSkill(e)) {
            if (re(e), "wind" === e) {
              var t = performance.now();
              this.windRuntime = new oK(this.phys), this.windRuntime.start(t), this.enterCasting("wind", [], oK.totalDurationMs());
              return
            }
            this.phys.enableSkillCeiling(), this.shakeCeilingActive = !0, this.enterCasting(e, [], oQ[e])
          }
        }
        startSwap(e, t) {
          var r, n, i = this.phys.captureKinematics(e),
            a = this.phys.captureKinematics(t);
          if (i && a) {
            var s = (h = i.position, m = a.position, h.x < m.x || h.x === m.x && h.y <= m.y),
              o = s ? [e, t] : [t, e],
              l = s ? i : a,
              c = s ? a : i,
              u = this.phys.getBody(o[0]),
              d = this.phys.getBody(o[1]);
            if (u && d) {
              var h, m, [p, g] = [oW(u, d), oW(d, u)];
              null == (r = (n = this.callbacks).onBodiesSuspended) || r.call(n, o), this.phys.suspend(o);
              var f = performance.now(),
                v = {
                  leftBodyId: o[0],
                  rightBodyId: o[1],
                  geometry: function(e, t) {
                    var r = arguments.length > 2 && void 0 !== arguments[2] ? arguments[2] : t,
                      n = arguments.length > 3 && void 0 !== arguments[3] ? arguments[3] : e,
                      i = t.x - e.x,
                      a = t.y - e.y,
                      s = Math.hypot(i, a),
                      o = s > .001 ? 1 / s : 0,
                      l = -a * o,
                      c = i * o,
                      u = Math.max(24, .4 * s);
                    return {
                      center: {
                        x: (e.x + t.x) / 2,
                        y: (e.y + t.y) / 2
                      },
                      leftPath: {
                        start: {
                          x: e.x,
                          y: e.y
                        },
                        control: {
                          x: (e.x + r.x) / 2 + l * u,
                          y: (e.y + r.y) / 2 + c * u
                        },
                        end: of({}, r)
                      },
                      rightPath: {
                        start: {
                          x: t.x,
                          y: t.y
                        },
                        control: {
                          x: (t.x + n.x) / 2 - l * u,
                          y: (t.y + n.y) / 2 - c * u
                        },
                        end: of({}, n)
                      }
                    }
                  }(l.position, c.position, p, g),
                  startedAt: f
                };
              this.swapRuntime = {
                ids: o,
                left: l,
                right: c,
                leftFrame: o$(l),
                rightFrame: o$(c),
                effect: v,
                startedAt: f,
                completed: !1,
                moveSfxPlayed: !1
              }, this.enterCasting("swap", o, oQ.swap, f), this.renderer.setSwapPath(v)
            }
          }
        }
        tickSwap(e) {
          var t, r, n = this.swapRuntime;
          if (n && !n.completed) {
            var i = ow(e - n.startedAt);
            if (i.moving && !n.moveSfxPlayed && (n.moveSfxPlayed = !0, re("swap")), i.moving) {
              var a = i.moveProgress;
              ox(n.effect.geometry.leftPath, a, n.leftFrame.position), n.leftFrame.angle = (t = n.left.angle, t + (n.right.angle - t) * a), ox(n.effect.geometry.rightPath, a, n.rightFrame.position), n.rightFrame.angle = (r = n.right.angle, r + (n.left.angle - r) * a), this.phys.setKinematics(n.ids[0], n.leftFrame, !1), this.phys.setKinematics(n.ids[1], n.rightFrame, !1), i.complete && (n.completed = !0, this.phys.resume(n.ids), this.phys.wakeAll(), this.settleSkill("swap"))
            }
          }
        }
        enterCasting(e, t, r) {
          var n = arguments.length > 3 && void 0 !== arguments[3] ? arguments[3] : performance.now();
          e5().setSkillSession({
            id: e,
            phase: "casting",
            hoverBodyId: null,
            selectedBodyIds: t,
            startedAt: n,
            endsAt: n + r
          }), this.restorePreview()
        }
        settleSkill(e) {
          var t, r, n = e5(),
            i = e1[e];
          if (null != i.chargeRequired) {
            if (n.swapCharge < i.chargeRequired) return this.cancel(), !1;
            n.consumeSwapCharge()
          } else if (!n.spendEnergy(i.cost, {
              deduct: !0
            })) return this.cancel(), !1;
          return n.incSkillUse(), null == (t = (r = this.callbacks).onSkillUsed) || t.call(r, e), !0
        }
        finish() {
          (!this.swapRuntime || this.swapRuntime.completed) && (this.swapRuntime = null, this.windRuntime && (this.windRuntime.end(!1), this.windRuntime = null), oT(), this.endShake(), this.renderer.setSwapPath(null), e5().setSkillSession(null), this.restorePreview())
        }
        restorePreview() {
          var e = e5();
          if ("playing" === e.state && !e.paused) {
            if (this.callbacks.onPreviewRestore) return void this.callbacks.onPreviewRestore();
            this.renderer.showPreview()
          }
        }
        endShake() {
          this.shakeCeilingActive && (this.phys.disableSkillCeiling(), this.shakeCeilingActive = !1)
        }
        constructor(e, t, r = {}) {
          oq(this, "phys", void 0), oq(this, "renderer", void 0), oq(this, "callbacks", void 0), oq(this, "swapRuntime", void 0), oq(this, "windRuntime", void 0), oq(this, "shakeCeilingActive", void 0), this.phys = e, this.renderer = t, this.callbacks = r, this.swapRuntime = null, this.windRuntime = null, this.shakeCeilingActive = !1
        }
      }

function o1(e, t, r) {
        return t in e ? Object.defineProperty(e, t, {
          value: r,
          enumerable: !0,
          configurable: !0,
          writable: !0
        }) : e[t] = r, e
      }

class o2 {
        start() {
          var e, t, r, n = arguments.length > 0 && void 0 !== arguments[0] ? arguments[0] : {},
            i = e5();
          this.tutorialDangerDemoActive = !1, this.renderer.clearTutorialDangerDemo(), this.skillController.cancel(!1), this.phys.clearBodies(), this.removedIds.clear(), this.pendingMerges = [], this.dropLockUntil = 0, this.lastCollisionSfxAt = -1 / 0, this.lastGamepadPreviewMoveAt = 0, this.spawnHistory = [], this.resetPhysicsClock(), i.setQueue(null != (e = n.firstSpawnLevel) ? e : this.nextSpawnLevel(), this.nextSpawnLevel()), i.start(), null == (t = (r = this.cb).onGameStart) || t.call(r), this.previewX = ey.container.width / 2, this.resetDangerCountdown(), this.renderer.resetDropZoneAnimation(), this.renderer.resetMergeMotions(), n.skipEntranceMotion ? this.renderer.showPreview() : this.renderer.showPreviewWithAppearMotion(500), this.renderer.setPreview(e5().current, this.previewX)
        }
        resize(e, t) {
          this.renderer.resize(e, t)
        }
        dropCurrent() {
          var e = e5();
          return !("playing" !== e.state || e.paused || this.skillController.isActive() || performance.now() < this.dropLockUntil) && (this.drop(this.previewX), !0)
        }
        showTutorialDangerDemo() {
          if (!this.tutorialDangerDemoActive && !this.destroyed) {
            for (var e of (this.skillController.cancel(!1), this.phys.clearBodies(), this.removedIds.clear(), this.pendingMerges = [], this.resetDangerCountdown(), this.renderer.resetMergeMotions(), sR)) {
              var t = this.phys.spawn(e.level, e.x, e.y);
              sG.Body.setAngle(t.body, e.angle), sG.Body.setVelocity(t.body, {
                x: 0,
                y: 0
              }), sG.Body.setAngularVelocity(t.body, 0), sG.Sleeping.set(t.body, !0)
            }
            this.phys.capturePreviousTransforms(), this.renderer.setForcedDangerDigit(3, !0), this.tutorialDangerDemoActive = !0
          }
        }
        startTutorialDangerDemoFadeOut() {
          this.tutorialDangerDemoActive && this.renderer.startTutorialDangerDemoFadeOut()
        }
        clearTutorialDangerDemo() {
          this.tutorialDangerDemoActive && (this.tutorialDangerDemoActive = !1, this.renderer.clearTutorialDangerDemo(), this.phys.clearBodies(), this.removedIds.clear(), this.pendingMerges = [], this.resetDangerCountdown())
        }
        movePreviewByGamepad(e) {
          var t = e5();
          if ("playing" !== t.state || t.paused || this.skillController.isPreparing() || !Number.isFinite(e)) return !1;
          var r = performance.now(),
            n = r - this.lastGamepadPreviewMoveAt;
          this.lastGamepadPreviewMoveAt = r;
          var i = this.clampX(this.previewX + 140 * e * ((n > 0 && n <= 100 ? n : 1e3 / 60) / 1e3));
          return this.movePreview(i), !0
        }
        tick(e, t) {
          var r, n = e5();
          if (this.tutorialDangerDemoActive) {
            this.physicsClock.reset(), this.skillController.isActive() && this.skillController.cancel(!1), this.renderer.render(this.phys.bodies);
            return
          }
          if ("playing" !== n.state) {
            this.physicsClock.reset(), this.dangerCountdown.isTracking() && this.resetDangerCountdown(), this.renderer.render(this.phys.bodies);
            return
          }
          if (n.paused) {
            this.physicsClock.reset(), this.skillController.isActive() && this.skillController.cancel(!1), this.dangerCountdown.pause(t), this.renderer.render(this.phys.bodies);
            return
          }
          if ((null == (r = n.skillSession) ? void 0 : r.id) === "swap" && "casting" === n.skillSession.phase || this.skillController.isPreparing()) this.physicsClock.reset(), this.skillController.tick(t), this.phys.capturePreviousTransforms();
          else
            for (var i = this.physicsClock.takeSteps(e), a = 0; a < i; a += 1) this.phys.capturePreviousTransforms(), this.skillController.tick(t), this.phys.update(), this.processMerges();
          n.tickEnergyDecay(t), this.checkGameOver(t), this.renderer.render(this.phys.bodies, this.physicsClock.interpolationAlpha)
        }
        resetPhysicsClock() {
          this.physicsClock.reset(), this.phys.capturePreviousTransforms(), this.frameRateLimiter.reset(performance.now())
        }
        processMerges() {
          if (0 !== this.pendingMerges.length) {
            var e = this.pendingMerges;
            this.pendingMerges = [];
            var t = e5();
            for (var r of e) {
              if (r.level > ek) {
                this.removedIds.delete(r.a.id), this.removedIds.delete(r.b.id);
                continue
              }
              var n, i, a, s, o = this.phys.bodies.get(r.a.id),
                l = this.phys.bodies.get(r.b.id),
                c = o3(o, r.a),
                u = o3(l, r.b);
              this.phys.remove(r.a.id, !1), this.phys.remove(r.b.id, !1), this.removedIds.delete(r.a.id), this.removedIds.delete(r.b.id);
              var d = r.level === ek,
                h = d ? ek : r.level + 1,
                m = (c.x + u.x) / 2,
                p = (c.y + u.y) / 2,
                g = [o, l].filter(e => !!e);
              if (d) this.renderer.playMergeFadeOut(g);
              else {
                var f = eM(h).collision,
                  v = function(e, t, r, n, i) {
                    var a = e.mass,
                      s = t.mass,
                      o = a + s,
                      l = (r.x * a + n.x * s) / o,
                      c = (r.y * a + n.y * s) / o,
                      u = Math.max(e.bounds.max.y, t.bounds.max.y);
                    c + i > u + .5 && (c = u - i);
                    var d = (e.velocity.x * a + t.velocity.x * s) / o;
                    return {
                      x: l,
                      y: c,
                      vx: d,
                      vy: (e.velocity.y * a + t.velocity.y * s) / o,
                      angularVelocity: (e.angularVelocity * a + t.angularVelocity * s) / o
                    }
                  }(r.a, r.b, c, u, f.bounds.maxY),
                  y = this.phys.spawn(h, v.x, v.y, {
                    velocity: {
                      x: v.vx,
                      y: v.vy
                    },
                    angularVelocity: v.angularVelocity
                  });
                this.renderer.playMerge(g, y), m = v.x, p = v.y
              }
              this.phys.wakeAll();
              var b = e5().energy;
              re("tuan_merge"), t.onMerge(h, r.level), e5().energy > b && re("energy_fill"), null == (n = (i = this.cb).onMerge) || n.call(i, h, m, p), d || h !== ek || null == (a = (s = this.cb).onGolden) || a.call(s)
            }
          }
        }
        discardPendingMerges(e) {
          var t = new Set(e);
          this.pendingMerges = this.pendingMerges.filter(e => {
            var r = t.has(e.a.id) || t.has(e.b.id);
            return r && (this.removedIds.delete(e.a.id), this.removedIds.delete(e.b.id)), !r
          })
        }
        checkGameOver(e) {
          var t, r, n = null != (t = null == (r = e5().skillSession) ? void 0 : r.phase) ? t : null;
          if ("casting" === n) return void this.resetDangerCountdown();
          if (null !== n) return void this.dangerCountdown.pause(e);
          this.dangerCountdown.resume(e);
          var i = !1;
          for (var a of this.phys.bodies.values()) {
            var {
              body: s
            } = a, {
              x: o,
              y: l
            } = s.velocity;
            if (function(e, t, r, n, i, a) {
                var s;
                return (s = e.max.y - e.min.y) > 0 && n - e.min.y >= s * i && t * t + r * r <= a * a
              }(s.bounds, o, l, ey.redLineY, ey.loseOverflowRatio, ey.loseSettleSpeed)) {
              i = !0;
              break
            }
          }
          var c = this.dangerCountdown.update(i, e, ey.loseCountdownMs);
          if (void 0 !== c) {
            if (null === c) {
              this.renderer.setDangerCountdown(null), this.lastDangerDigit = null, this.dangerCountdownSfxPlayed = !1;
              return
            }
            this.renderer.setDangerCountdown(Math.max(0, c));
            var u = ey.loseCountdownMs - Math.max(0, c),
              d = sI(u);
            d !== this.lastDangerDigit && (this.lastDangerDigit = d, this.dangerCountdownSfxPlayed = !1), null !== d && !this.dangerCountdownSfxPlayed && u % 1100 >= 280 && (this.dangerCountdownSfxPlayed = !0, re("danger_countdown")), c <= 0 && this.gameOver()
          }
        }
        resetDangerCountdown() {
          this.dangerCountdown.reset(), this.lastDangerDigit = null, this.dangerCountdownSfxPlayed = !1, this.renderer.setDangerCountdown(null)
        }
        gameOver() {
          var e, t, r = e5();
          "playing" === r.state && (this.skillController.cancel(!1), r.setState("over"), this.resetDangerCountdown(), this.renderer.hidePreview(), null == (e = (t = this.cb).onGameOver) || e.call(t))
        }
        bindInput() {
          this.inputTarget.addEventListener("pointermove", this.onPointerMove), this.inputTarget.addEventListener("pointerup", this.onPointerUp), this.inputTarget.addEventListener("pointerdown", this.onPointerMove), this.inputTarget.addEventListener("mousemove", this.onVirtualMouseMove), window.addEventListener("keydown", this.onKeyDown)
        }
        clampX(e) {
          var {
            collision: t
          } = eM(e5().current), r = q(t.primary);
          return Math.max(r.x - t.bounds.minX, Math.min(ey.container.width - (t.bounds.maxX - r.x), e))
        }
        movePreview(e) {
          var t, r, n = this.previewX;
          this.previewX = e, this.renderer.movePreview(e), e !== n && (null == (t = (r = this.cb).onPreviewMove) || t.call(r, e, n))
        }
        handlePointerMove(e, t) {
          var r = e5();
          if ("playing" === r.state && !r.paused) {
            var n = this.renderer.toWorldX(e),
              i = this.pointerWorldY(t);
            if (!this.skillController.pointerMove(n, i) && sW(n, i)) {
              var a = this.clampX(n);
              this.movePreview(a)
            }
          }
        }
        pointerWorldY(e) {
          return this.renderer.toWorldY(e)
        }
        drop(e) {
          var t, r, n = e5(),
            i = n.current,
            {
              collision: a
            } = eM(i),
            s = sV(a, e);
          this.movePreview(e), this.phys.spawn(i, s.x, s.y), re("tuan_drop"), null == (t = (r = this.cb).onDrop) || t.call(r, i), n.advanceQueue(this.nextSpawnLevel()), this.dropLockUntil = performance.now() + ey.dropCooldownMs, this.renderer.playDropZoneAnimation(), this.renderer.hidePreview(), window.setTimeout(() => {
            this.destroyed || "playing" !== e5().state || e5().paused || this.skillController.isActive() || (this.previewX = this.clampX(this.previewX), this.renderer.showPreviewWithAppearMotion(), this.renderer.setPreview(e5().current, this.previewX))
          }, ey.dropCooldownMs)
        }
        refreshPreview() {
          var e = e5();
          "playing" === e.state && (this.previewX = this.clampX(this.previewX), this.renderer.setPreview(e.current, this.previewX))
        }
        restorePreviewControl() {
          var e = e5();
          "playing" !== e.state || e.paused || (this.previewX = this.clampX(this.previewX), this.renderer.setPreview(e.current, this.previewX), this.renderer.showPreview())
        }
        dropRandomTuans(e) {
          if ("playing" === e5().state && Number.isFinite(e))
            for (var t = Math.min(100, Math.max(1, Math.floor(e))), {
                width: r
              } = ey.container, n = 0; n < t; n += 1) {
              var i = Math.floor(5 * Math.random()) + 1,
                {
                  bounds: a
                } = eM(i).collision,
                s = Math.min(-a.minX, r / 2),
                o = Math.max(s, r - a.maxX),
                l = s + Math.random() * (o - s),
                c = ey.dropY - Math.random() * ey.dropZoneHeight;
              this.phys.spawn(i, l, c)
            }
        }
        captureTuanLayout() {
          return Array.from(this.phys.bodies.values()).map(e => {
            var t = o3(e, e.body);
            return {
              level: e.level,
              x: o5(t.x),
              y: o5(t.y),
              angle: o5(e.body.angle)
            }
          })
        }
        applyGravity(e) {
          this.phys.engine.gravity.y = e
        }
        applyBodyProps() {
          this.phys.syncBodyProps()
        }
        applyWallProps() {
          this.phys.syncWallProps()
        }
        applyTuanScale(e) {
          Number.isFinite(e) && !(e <= 0) && (this.phys.scaleTuans(e), this.movePreview(this.clampX(this.previewX)))
        }
        activateSkill(e) {
          return this.skillController.activate(e)
        }
        confirmSkill() {
          return this.skillController.confirm()
        }
        cancelSkill() {
          this.skillController.cancel()
        }
        nextSpawnLevel() {
          var {
            spawnLevelMin: e,
            spawnLevelMax: t
          } = ey, r = t - e + 1, n = e + Math.floor(Math.random() * r), i = this.spawnHistory[this.spawnHistory.length - 1], a = this.spawnHistory[this.spawnHistory.length - 2];
          return r > 1 && i === a && n === i && (n = e + Math.floor(Math.random() * (r - 1))) >= i && (n += 1), this.spawnHistory.push(n), this.spawnHistory.length > 2 && this.spawnHistory.shift(), n
        }
        destroy() {
          this.destroyed = !0, cancelAnimationFrame(this.animationFrame), sG.Events.off(this.phys.engine, "collisionStart", this.onCollision), document.removeEventListener("visibilitychange", this.onVisibilityChange), this.inputTarget.removeEventListener("pointermove", this.onPointerMove), this.inputTarget.removeEventListener("pointerup", this.onPointerUp), this.inputTarget.removeEventListener("pointerdown", this.onPointerMove), this.inputTarget.removeEventListener("mousemove", this.onVirtualMouseMove), window.removeEventListener("keydown", this.onKeyDown), this.skillController.destroy(), this.phys.destroy(), this.renderer.destroy()
        }
        constructor(e, t, r, n = {}, i = {}, a = e) {
          o1(this, "phys", new sq), o1(this, "renderer", void 0), o1(this, "skillController", void 0), o1(this, "cb", void 0), o1(this, "inputTarget", void 0), o1(this, "pendingMerges", []), o1(this, "removedIds", new Set), o1(this, "dangerCountdown", new sF), o1(this, "dropLockUntil", 0), o1(this, "lastCollisionSfxAt", -1 / 0), o1(this, "lastDangerDigit", null), o1(this, "dangerCountdownSfxPlayed", !1), o1(this, "tutorialDangerDemoActive", !1), o1(this, "previewX", void 0), o1(this, "lastGamepadPreviewMoveAt", 0), o1(this, "spawnHistory", []), o1(this, "destroyed", !1), o1(this, "animationFrame", 0), o1(this, "physicsClock", new sY), o1(this, "frameRateLimiter", void 0), o1(this, "loop", e => {
            if (!this.destroyed) {
              var t = this.frameRateLimiter.takeElapsed(e);
              null !== t && this.tick(t, e), this.animationFrame = requestAnimationFrame(this.loop)
            }
          }), o1(this, "onCollision", e => {
            var t = !1;
            for (var r of e.pairs) {
              var n, i, a = sZ(r.bodyA),
                s = sZ(r.bodyB);
              if (a.id !== s.id) {
                var o = "tuan" === a.label && "tuan" === s.label,
                  l = null == (n = a.plugin) ? void 0 : n.level,
                  c = null == (i = s.plugin) ? void 0 : i.level;
                if (o && !this.removedIds.has(a.id) && !this.removedIds.has(s.id) && l && c && l === c && l <= ek) {
                  this.removedIds.add(a.id), this.removedIds.add(s.id), this.pendingMerges.push({
                    a,
                    b: s,
                    level: l
                  });
                  continue
                }
                var u = a.velocity.x - s.velocity.x,
                  d = a.velocity.y - s.velocity.y,
                  h = Math.abs(u * r.collision.normal.x + d * r.collision.normal.y);
                ("tuan" === a.label || "tuan" === s.label) && "skill-ceiling" !== a.label && "skill-ceiling" !== s.label && h >= .8 && (t = !0)
              }
            }
            var m = performance.now();
            t && m - this.lastCollisionSfxAt >= 90 && (this.lastCollisionSfxAt = m, re("tuan_collision"))
          }), o1(this, "onVisibilityChange", () => {
            this.resetPhysicsClock()
          }), o1(this, "onPointerMove", e => {
            this.handlePointerMove(e.clientX, e.clientY)
          }), o1(this, "onVirtualMouseMove", e => {
            e.virtual && this.handlePointerMove(e.clientX, e.clientY)
          }), o1(this, "onPointerUp", e => {
            var t = e5();
            if ("playing" === t.state && !t.paused) {
              var r = this.renderer.toWorldX(e.clientX),
                n = this.pointerWorldY(e.clientY);
              this.skillController.pointerUp(r, n) || !sW(r, n) || performance.now() < this.dropLockUntil || this.drop(this.clampX(r))
            }
          }), o1(this, "onKeyDown", e => {
            if (("Space" === e.code || " " === e.key) && !e.repeat) {
              var t = e5();
              "playing" !== t.state || t.paused || this.skillController.isActive() || performance.now() < this.dropLockUntil || (e.preventDefault(), this.drop(this.previewX))
            }
          }), this.inputTarget = a;
          var s = performance.now();
          this.frameRateLimiter = new sX((e => e ? 30 : 60)(tC()), s), this.renderer = new oH(e, t, r, i), this.cb = n, this.skillController = new o0(this.phys, this.renderer, {
            onSkillUsed: e => {
              var t, r;
              return null == (t = (r = this.cb).onSkillUsed) ? void 0 : t.call(r, e)
            },
            onBodiesSuspended: e => this.discardPendingMerges(e),
            onPreviewRestore: () => this.restorePreviewControl()
          }), this.previewX = ey.container.width / 2, sG.Events.on(this.phys.engine, "collisionStart", this.onCollision), document.addEventListener("visibilitychange", this.onVisibilityChange), this.bindInput(), this.animationFrame = requestAnimationFrame(this.loop)
        }
      }

function o3(e, t) {
        if (!e) return function(e) {
          for (var t = 1; t < arguments.length; t++) {
            var r = null != arguments[t] ? arguments[t] : {},
              n = Object.keys(r);
            "function" == typeof Object.getOwnPropertySymbols && (n = n.concat(Object.getOwnPropertySymbols(r).filter(function(e) {
              return Object.getOwnPropertyDescriptor(r, e).enumerable
            }))), n.forEach(function(t) {
              o1(e, t, r[t])
            })
          }
          return e
        }({}, t.position);
        var r = Math.cos(e.body.angle),
          n = Math.sin(e.body.angle);
        return {
          x: e.body.position.x + e.renderOffset.x * r - e.renderOffset.y * n,
          y: e.body.position.y + e.renderOffset.x * n + e.renderOffset.y * r
        }
      }

function o5(e) {
        return Math.round(1e3 * e) / 1e3
      }
// The host owns cadence. Virtual timers are drained only by advance(), so a
// hidden HUD cannot wake JavaScriptCore or advance a skill/game-over countdown.
var __game=null,__nativeRandom=Math.random;
function __seed(seed){if(seed==null){Math.random=__nativeRandom;return;}var s=seed>>>0;Math.random=function(){s=(s+0x6d2b79f5)|0;var t=Math.imul(s^(s>>>15),1|s);t^=t+Math.imul(t^(t>>>7),61|t);return((t^(t>>>14))>>>0)/4294967296;};}
function __create(){if(__game)__game.destroy();__timers=[];__game=new o2(window,230,280,{onGameOver:function(){e5().commitHighScore();}},{});}
var OrbiPom={
 start:function(seed){__seed(seed);__now=1;__create();__game.start({skipEntranceMotion:true});return this.snapshot();},
 move:function(x,y){if(__game&&Number.isFinite(x)&&Number.isFinite(y))__game.handlePointerMove(x,y);return this.interaction();},
 interaction:function(){var state=e5(),g=__game,r=g&&g.renderer,p=sV(eM(state.current).collision,g?g.previewX:115),scale=r&&r.previewAppearAt!=null?s3(ogNative((__now-r.previewAppearAt)/300)):1;return {previewX:p.x,previewY:p.y-26*(1-scale),previewSize:eM(state.current).size,previewScale:scale,previewVisible:!!r&&r.previewVisible,hoverBodyID:state.skillSession?state.skillSession.hoverBodyId:null};},
 pointerUp:function(x,y){if(__game&&Number.isFinite(x)&&Number.isFinite(y))__game.onPointerUp({clientX:x,clientY:y});},
 drop:function(){return !!__game&&__game.dropCurrent();},
 activate:function(skill){return !!__game&&!!e1[skill]&&__game.activateSkill(skill);},
 cancelSkill:function(){if(__game)__game.cancelSkill();},
 pause:function(paused){if(!__game||e5().paused===paused)return;e5().setPaused(paused);__game.resetPhysicsClock();if(paused)__game.tick(0,__now);else{__game.dangerCountdown.resume(__now);__game.restorePreviewControl();}},
 advance:function(seconds){if(!__game||e5().paused||e5().state!=='playing'||!Number.isFinite(seconds)||seconds<=0)return this.snapshot();var ms=seconds*1000;__now+=ms;var due=__timers.filter(function(t){return t.at<=__now;});__timers=__timers.filter(function(t){return t.at>__now;});for(var t of due)t.fn();__game.tick(ms,__now);return this.snapshot();},
 highScore:function(score){if(Number.isFinite(score))e5().hydrateProfile(Math.min(99999,Math.max(0,Math.floor(score))),5,false);},
 snapshot:function(){var s=e5(),g=__game,r=g&&g.renderer,bodies=[];if(g){for(var b of g.phys.bodies.values()){var p=sQ(b,r.alpha),c=Math.cos(p.angle),a=Math.sin(p.angle),started=r.mergeMotions.get(b.id),motion=started===undefined?null:s5(__now-started);if(motion&&motion.complete)r.mergeMotions.delete(b.id);bodies.push({id:b.id,level:b.level,x:p.position.x+b.renderOffset.x*c-b.renderOffset.y*a,y:p.position.y+b.renderOffset.x*a+b.renderOffset.y*c,angle:p.angle,size:eM(b.level).size,opacity:1,scale:motion?motion.incomingScale:1});}r.ghosts=r.ghosts.filter(function(b){var age=__now-b.startedAt;if(b.kind==='clear'){var p=om(ogNative(age/400));b.opacity=1-p;b.scale=1-.5*p;return age<500;}var m=s5(age);b.opacity=m.outgoingOpacity;return !m.complete;});for(var ghost of r.ghosts)if(ghost.opacity>0)bodies.push(ghost);}
 var preview=this.interaction(),skill=s.skillSession,wind=g&&g.skillController.windRuntime;
 return{state:s.state,paused:s.paused,score:s.score,highScore:s.highScore,energy:s.energy,energyProgress:e2(s,__now),swapCharge:s.swapCharge,mergeCount:s.mergeCount,skillUseCount:s.skillUseCount,currentLevel:s.current,nextLevel:s.next,previewX:preview.previewX,previewY:preview.previewY,previewSize:preview.previewSize,previewVisible:preview.previewVisible,previewScale:preview.previewScale,dangerSeconds:r&&r.dangerCountdownMs!=null?r.dangerCountdownMs/1000:null,skill:skill?skill.id:null,skillPhase:skill?skill.phase:null,selectedBodyIDs:skill?skill.selectedBodyIds:[],hoverBodyID:skill?skill.hoverBodyId:null,windSurfaceY:wind&&wind.state?wind.state.surfaceY:null,bodies:bodies,simulationTime:__now/1000};},
 destroy:function(){if(__game)__game.destroy();__game=null;__timers=[];}
};
function ogNative(v){return Math.min(1,Math.max(0,v));}
