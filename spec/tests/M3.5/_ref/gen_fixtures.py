#!/usr/bin/env python3
"""M3.5 fixture generator (reference data only -- NOT product code).

Every line:col is computed from anchors in the source text; every computed
stdout is produced by a Python mirror.  Re-run:  python3 gen_fixtures.py
(then python3 validate.py).  Spec section numbers cited in comments refer to
spec/M3.5-concurrency.md.
"""
import os, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)
M3 = os.path.join(OUT, "..", "M3")
COUNT = {}

def pos(src, needle, nth=1, off=0):
    idx = -1
    for _ in range(nth):
        idx = src.index(needle, idx + 1)
    line = src.count("\n", 0, idx) + 1
    col = idx - (src.rfind("\n", 0, idx) + 1) + 1 + off
    return line, col

def A(needle, nth=1, off=0):
    return (needle, nth, off)

def w(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        f.write(text)

def fx(name, src, kind, exit_=0, diags=(), stdout=None, stderr=None, repeat=None,
       threads=None, flags=None, exact=False, comment=None, files=None, main="main.fm",
       png=None):
    """diags: list of (severity, code, primary_anchor, note_anchor|None)."""
    if files:                       # multi-file: name is a directory
        for fn, txt in files.items():
            w(f"{OUT}/{name}/{fn}", txt)
        src_main, prefix = files[main], f"{name}/{main}:"
    else:
        w(f"{OUT}/{name}.fm", src)
        src_main, prefix = src, ""
    e = f"# kind: {kind}\n# exit: {exit_}\n"
    if flags: e += f"# flags: {flags}\n"
    if repeat: e += f"# repeat: {repeat}\n"
    if threads: e += f"# threads: {threads}\n"
    if exact: e += "# diag_exact: true\n"
    if png: e += f"# png: {png[0]}\n# sha256: {png[1]}\n"
    if comment:
        for c in comment.split("\n"): e += f"## {c}\n"
    for sev, code, prim, note in diags:
        l, c = pos(src_main, *prim)
        e += f"# {sev}: {prefix}{l}:{c}: {code}\n"
        if note:
            nl, nc = pos(src_main, *note)
            e += f"# note: {prefix}{nl}:{nc}\n"
    if stdout is not None: e += "# stdout:\n" + stdout + "# end\n"
    if stderr is not None: e += "# stderr:\n" + stderr + "# end\n"
    w(f"{OUT}/{name}.expected", e)
    COUNT[kind] = COUNT.get(kind, 0) + 1

def run(name, src, out, **kw):        fx(name, src, "run", 0, stdout=out, **kw)
def cerr(name, src, code, prim, note=None, sev="error", **kw):
    fx(name, src, "compile_error", 1, diags=[(sev, code, prim, note)], **kw)
def trap(name, src, code, msg, stdout=None, **kw):
    fx(name, src, "runtime_trap", code, stdout=stdout, stderr=f"runtime error: {msg}\n", **kw)
def runw(name, src, out, warns, **kw):   # run + warnings
    fx(name, src, "run", 0, diags=[("warning", c, p, n) for c, p, n in warns], stdout=out, **kw)

COUNTER = """class Counter {
  n: int;

  constructor(n: int) {
    this.n = n;
  }

  inc(): void {
    this.n = this.n + 1;
  }

  get(): int {
    return this.n;
  }
}
"""
COUNTER_MIN = """class Counter {
  n: int;

  constructor(n: int) {
    this.n = n;
  }
}
"""
PAIR = """struct Pair {
  left: int;
  right: int;
}
"""
FLAG = """class Flag {
  on: int;

  constructor() {
    this.on = 0;
  }
}
"""
def two_task(b1, b2, decl="  let a: int = 0;\n", tail="  println(a);\n"):
    return ("function main(): int {\n" + decl + "  parallel {\n    task {\n" + b1 +
            "    }\n    task {\n" + b2 + "    }\n  }\n" + tail + "  return 0;\n}\n")
SCENE_IMP = 'import { Mesh, BoxGeometry, MeshBasicMaterial } from "farmos:scene";\n'
MESH = 'new Mesh(new BoxGeometry(1, 1, 1), new MeshBasicMaterial(0xff0000))'

# =========================================================== CLASS 1: no conflict (run)
run("001_two_tasks_basic", """function fib(n: int): int {
  if (n < 2) {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

function main(): int {
  let a: int = 0;
  let b: int = 0;
  parallel {
    task {
      a = fib(20);
    }
    task {
      b = fib(21);
    }
  }
  println(a);
  println(b);
  println(a + b);
  return 0;
}
""", "6765\n10946\n17711\n", repeat=20, threads="1,2",
    comment="Reference 2-task demo for the AC-M35-16 size budget (no output in tasks, no dynamic checks: none exist in M3.5, section 6).")

run("002_fork_join_visibility", """function main(): int {
  let x: int = 5;
  let y: int = 0;
  let z: int = 0;
  parallel {
    task {
      y = x * 2;
    }
    task {
      z = x + 100;
    }
  }
  println(y);
  println(z);
  x = y + z;
  parallel {
    task {
      y = x + 1;
    }
    task {
      z = x + 2;
    }
  }
  println(y);
  println(z);
  return 0;
}
""", "10\n105\n116\n117\n", comment="Fork sees parent state (4.5); join publishes task writes (4.5); a second block sees the first block's results.")

run("003_task_private_locals", """function main(): int {
  let out0: int = 0;
  let out1: int = 0;
  let t: int = 1000;
  parallel {
    task {
      let t: int = 1;
      let s: int = 0;
      for (let i = 0; i < 10; i += 1) {
        s += i * t;
      }
      out0 = s;
    }
    task {
      let t: int = 2;
      let s: int = 0;
      for (let i = 0; i < 10; i += 1) {
        s += i * t;
      }
      out1 = s;
    }
  }
  println(out0);
  println(out1);
  println(t);
  return 0;
}
""", "45\n90\n1000\n", comment="Locals declared inside a task are task-private (4.3) even when they shadow an outer name.")

run("004_shared_reads", """struct Point {
  x: int;
  y: int;
}

class Config {
  scale: int;

  constructor(scale: int) {
    this.scale = scale;
  }
}

function main(): int {
  const data: int[4] = [1, 2, 3, 4];
  const p: Point = new Point(3, 4);
  const cfg: Config = new Config(10);
  let sum: int = 0;
  let prod: int = 0;
  parallel {
    task {
      let s: int = 0;
      for (let i = 0; i < 4; i += 1) {
        s += data[i] * cfg.scale;
      }
      sum = s + p.x;
    }
    task {
      let m: int = 1;
      for (let i = 0; i < 4; i += 1) {
        m *= data[i];
      }
      prod = m * cfg.scale + p.y;
    }
  }
  println(sum);
  println(prod);
  return 0;
}
""", "103\n244\n", comment="Read-only sharing of an array, a struct and a class field is always legal (6.1 class 1).")

run("005_disjoint_struct_fields", PAIR + """
function main(): int {
  let p: Pair = new Pair(0, 0);
  parallel {
    task {
      p.left = 6 * 7;
    }
    task {
      p.right = 50 + 8;
    }
  }
  let q: Pair = p;
  println(q.left);
  println(q.right);
  println(q.left + q.right);
  return 0;
}
""", "42\n58\n100\n", comment="Sibling struct fields are distinct locations (5.5).")

run("006_disjoint_array_const_index", """function main(): int {
  let r: int[4] = [0, 0, 0, 0];
  parallel {
    task {
      r[0] = 10;
    }
    task {
      r[1] = 20;
    }
    task {
      r[2] = 30;
    }
    task {
      r[3] = 40;
    }
  }
  println(r[0] + r[1] + r[2] + r[3]);
  println(r[2]);
  return 0;
}
""", "100\n30\n", comment="Different constant indices are disjoint (5.4); more than two tasks.")

run("007_loop_interval_disjoint", """function main(): int {
  let a: int[8] = [0, 0, 0, 0, 0, 0, 0, 0];
  parallel {
    task {
      for (let i = 0; i < 4; i += 1) {
        a[i] = i * i;
      }
    }
    task {
      for (let i = 4; i < 8; i += 1) {
        a[i] = i * i;
      }
    }
  }
  let total: int = 0;
  for (let k = 0; k < 8; k += 1) {
    total += a[k];
  }
  println(total);
  println(a[3]);
  println(a[7]);
  return 0;
}
""", "140\n9\n49\n", repeat=20, threads="1,2,4",
    comment="Counted-loop intervals [0,3] and [4,7] are provably disjoint (5.4). This is the ONLY way dynamic-looking indices are accepted under OQ-M35-02 option A.")

run("008_class_field_disjoint", """class Stats {
  lo: int;
  hi: int;

  constructor() {
    this.lo = 0;
    this.hi = 0;
  }
}

function main(): int {
  const s: Stats = new Stats();
  parallel {
    task {
      s.lo = 3;
    }
    task {
      s.hi = 9;
    }
  }
  println(s.lo);
  println(s.hi);
  return 0;
}
""", "3\n9\n", comment="Object-field granularity: two fields of one object are independent locations (5.1).")

run("009_class_refs_distinct_objects", COUNTER + """
function bump(c: Counter, times: int): void {
  for (let i = 0; i < times; i += 1) {
    c.inc();
  }
}

function main(): int {
  const a: Counter = new Counter(0);
  const b: Counter = new Counter(100);
  parallel {
    task {
      bump(a, 5);
    }
    task {
      bump(b, 7);
    }
  }
  println(a.get());
  println(b.get());
  return 0;
}
""", "5\n107\n", repeat=20, threads="1,2,4",
    comment="a and b come from two different `new` sites, hence provably different objects (5.3); the summary of bump (7.1) instantiated at both call sites is disjoint.")

run("010_print_order", """function work(n: int): int {
  let s: int = 0;
  for (let i = 0; i < n; i += 1) {
    s += i;
  }
  return s;
}

function main(): int {
  println("before");
  parallel {
    task {
      println("t1 start");
      println(work(200000));
      println("t1 end");
    }
    task {
      println("t2 start");
      println(work(10));
      println("t2 end");
    }
    task {
      print("t3 ");
      println("only");
    }
  }
  println("after");
  return 0;
}
""", "before\nt1 start\n19999900000\nt1 end\nt2 start\n45\nt2 end\nt3 only\nafter\n",
    repeat=20, threads="1,2,3,8",
    comment="Task 1 is slowest but its output is first: per-task output flushed in task-index order at join (9).")

run("011_param_copy_private", """struct Point {
  x: int;
  y: int;
}

function shift(p: Point): int {
  let q: Point = p;
  q.x = q.x + 100;
  return q.x;
}

function main(): int {
  const pt: Point = new Point(1, 2);
  let r1: int = 0;
  let r2: int = 0;
  parallel {
    task {
      r1 = shift(pt);
    }
    task {
      r2 = shift(pt);
    }
  }
  println(r1);
  println(r2);
  println(pt.x);
  return 0;
}
""", "101\n101\n1\n", comment="Struct arguments are copies (M1 4.6): the callee's writes are callee-private and absent from its summary (7.1).")

run("012_struct_method_disjoint", """struct Acc {
  total: int;

  add(v: int): void {
    this.total = this.total + v;
  }
}

function main(): int {
  let a: Acc = new Acc(0);
  let b: Acc = new Acc(0);
  parallel {
    task {
      for (let i = 0; i < 5; i += 1) {
        a.add(i);
      }
    }
    task {
      for (let i = 0; i < 5; i += 1) {
        b.add(i * 10);
      }
    }
  }
  println(a.total);
  println(b.total);
  return 0;
}
""", "10\n100\n", comment="Struct methods take `this` by reference (M2 3.2): the effect lands on the receiver path a.total / b.total; distinct value variables never alias (5.3).")

run("013_nested_parallel", """function main(): int {
  let r: int[4] = [0, 0, 0, 0];
  parallel {
    task {
      parallel {
        task {
          r[0] = 1;
        }
        task {
          r[1] = 2;
        }
      }
      r[0] = r[0] + r[1] * 10;
    }
    task {
      parallel {
        task {
          r[2] = 3;
        }
        task {
          r[3] = 4;
        }
      }
      r[2] = r[2] + r[3] * 10;
    }
  }
  println(r[0]);
  println(r[1]);
  println(r[2]);
  println(r[3]);
  return 0;
}
""", "21\n2\n43\n4\n", repeat=20, threads="1,2,4", comment="Nested blocks (6.9): accesses of an inner block count for the enclosing task; here outer tasks touch {r0,r1} and {r2,r3}.")

run("014_parallel_in_loop", """function main(): int {
  let a: int = 0;
  let b: int = 0;
  for (let round = 1; round <= 3; round += 1) {
    parallel {
      task {
        a = a + round;
      }
      task {
        b = b + round * 10;
      }
    }
    println(a + b);
  }
  return 0;
}
""", "11\n33\n66\n", comment="Each execution of the parallel statement is an independent fork/join; `round` is a shared read.")

run("015_eight_tasks", """function sq(n: int): int {
  return n * n;
}

function main(): int {
  let r: int[8] = [0, 0, 0, 0, 0, 0, 0, 0];
  parallel {
    task {
      r[0] = sq(1);
    }
    task {
      r[1] = sq(2);
    }
    task {
      r[2] = sq(3);
    }
    task {
      r[3] = sq(4);
    }
    task {
      r[4] = sq(5);
    }
    task {
      r[5] = sq(6);
    }
    task {
      r[6] = sq(7);
    }
    task {
      r[7] = sq(8);
    }
  }
  let sum: int = 0;
  for (let i = 0; i < 8; i += 1) {
    sum += r[i];
  }
  println(r[0]);
  println(r[7]);
  println(sum);
  return 0;
}
""", "1\n64\n204\n", repeat=20, threads="1,2,3,8")

def collatz(n):
    steps = 0; v = n
    while v != 1:
        v = v // 2 if v % 2 == 0 else v * 3 + 1
        steps += 1
    return steps
chk = 0; last = None
for it in range(1, 201):
    r = [collatz(it), collatz(it + 1000), collatz(it * 3), collatz(it + 7)]
    chk = (chk * 31 + r[0] + r[1] * 2 + r[2] * 3 + r[3] * 4) % 1000003
    last = r
run("016_stress_200_iterations", """function collatz(n: int): int {
  let steps: int = 0;
  let v: int = n;
  while (v != 1) {
    if (v % 2 == 0) {
      v = v / 2;
    } else {
      v = v * 3 + 1;
    }
    steps += 1;
  }
  return steps;
}

function main(): int {
  let checksum: int = 0;
  let r: int[4] = [0, 0, 0, 0];
  for (let iter = 1; iter <= 200; iter += 1) {
    parallel {
      task {
        r[0] = collatz(iter);
      }
      task {
        r[1] = collatz(iter + 1000);
      }
      task {
        r[2] = collatz(iter * 3);
      }
      task {
        r[3] = collatz(iter + 7);
      }
    }
    checksum = (checksum * 31 + r[0] + r[1] * 2 + r[2] * 3 + r[3] * 4) % 1000003;
  }
  println(checksum);
  println(r[0] + r[1] + r[2] + r[3]);
  return 0;
}
""", f"{chk}\n{sum(last)}\n", repeat=5, threads="1,2,4,8",
    comment="200 fork/joins inside one process (AC-M35-10). Any schedule dependence would perturb the checksum.")

def f17(n):
    s = 0
    for i in range(n): s += (i * i) % 7
    return s
run("017_serial_equivalence", """function f(n: int): int {
  let s: int = 0;
  for (let i = 0; i < n; i += 1) {
    s += i * i % 7;
  }
  return s;
}

function main(): int {
  let p0: int = 0;
  let p1: int = 0;
  let p2: int = 0;
  parallel {
    task {
      p0 = f(1000);
    }
    task {
      p1 = f(2000);
    }
    task {
      p2 = f(3000);
    }
  }
  let q0: int = f(1000);
  let q1: int = f(2000);
  let q2: int = f(3000);
  println(p0);
  println(p1);
  println(p2);
  println(p0 == q0 && p1 == q1 && p2 == q2);
  return 0;
}
""", f"{f17(1000)}\n{f17(2000)}\n{f17(3000)}\ntrue\n", comment="Sequential equivalence (4.6): a conflict-free block equals running its tasks one after another in index order.")

run("018_task_reads_own_writes", """function main(): int {
  let a: int = 1;
  let b: int = 1;
  parallel {
    task {
      a = a + 1;
      a = a * 10;
      a += 5;
      println(a);
    }
    task {
      b = 2;
      b = b + b;
      println(b);
    }
  }
  println(a);
  println(b);
  return 0;
}
""", "25\n4\n25\n4\n", comment="Write/write and read-after-write inside ONE task are fine (6.6).")

run("019_struct_copy_in_task", PAIR + """
function main(): int {
  const p: Pair = new Pair(1, 2);
  let o1: int = 0;
  let o2: int = 0;
  parallel {
    task {
      let q: Pair = p;
      q.left = 99;
      o1 = q.left + q.right;
    }
    task {
      o2 = p.left + p.right;
    }
  }
  println(o1);
  println(o2);
  println(p.left);
  return 0;
}
""", "101\n3\n1\n", comment="A struct copy declared inside a task is task-private (4.3, M1 4.6).")

run("020_string_results", """function main(): int {
  let s1: string = "";
  let s2: string = "";
  parallel {
    task {
      s1 = "alpha" + "-" + str(1);
    }
    task {
      s2 = "beta" + "-" + str(2);
    }
  }
  println(s1 + "," + s2);
  println(len(s1) + len(s2));
  return 0;
}
""", "alpha-1,beta-2\n13\n")

run("021_dynamic_push_disjoint", """function main(): int {
  let xs: int[] = [];
  let ys: int[] = [];
  parallel {
    task {
      for (let i = 0; i < 3; i += 1) {
        push(xs, i);
      }
    }
    task {
      for (let i = 0; i < 4; i += 1) {
        push(ys, i * 2);
      }
    }
  }
  println(len(xs));
  println(len(ys));
  println(xs[2] + ys[3]);
  return 0;
}
""", "3\n4\n8\n", comment="push writes its array (6.7): two different arrays are independent; many pushes inside one task are fine.")

sha002 = "667223dfd9984713ee4ddadf2a41fa448efe8be267c6c5a88b1783a8ae1702d1"
src022 = """import { Scene, PerspectiveCamera, BoxGeometry, MeshBasicMaterial, Mesh, Renderer } from "farmos:scene";
import { Color } from "farmos:math";

function main(): int {
  const geometry: BoxGeometry = new BoxGeometry(1, 1, 1);
  const material: MeshBasicMaterial = new MeshBasicMaterial(0xff0000);

  const sceneA: Scene = new Scene();
  sceneA.setBackground(new Color(0x202020));
  const cameraA: PerspectiveCamera = new PerspectiveCamera(75, 1, 0.1, 1000);
  cameraA.position.z = 3;
  cameraA.lookAt(0, 0, 0);
  const cubeA: Mesh = new Mesh(geometry, material);
  sceneA.add(cubeA);
  const rendererA: Renderer = new Renderer(128, 128);

  const sceneB: Scene = new Scene();
  sceneB.setBackground(new Color(0x111111));
  const cameraB: PerspectiveCamera = new PerspectiveCamera(60, 1, 0.1, 1000);
  cameraB.position.z = 5;
  cameraB.lookAt(0, 0, 0);
  const cubeB: Mesh = new Mesh(geometry, material);
  cubeB.rotation.y = 0.5;
  sceneB.add(cubeB);
  const rendererB: Renderer = new Renderer(64, 64);

  parallel {
    task {
      rendererA.render(sceneA, cameraA);
    }
    task {
      rendererB.render(sceneB, cameraB);
    }
  }
  rendererA.savePNG("out.png");
  println("done");
  return 0;
}
"""
fx("022_two_renderers_png", src022, "run_png", 0, repeat=5, threads="1,2", png=("out.png", sha002),
   stdout="done\n",
   comment="Image A is byte-identical to M3 fixture 002_unlit_cube; image B renders concurrently and is discarded.\nDistinct renderer/scene/camera `new` sites: Disjoint (8.3). The shared geometry/material are only read by both renders. savePNG is outside the block (E0805 applies inside tasks only).")
shutil.copyfile(os.path.join(M3, "002_unlit_cube.golden.png"), f"{OUT}/022_two_renderers_png.golden.png")

run("023_parallel_in_method", """class Sim {
  a: int;
  b: int;

  constructor() {
    this.a = 1;
    this.b = 1;
  }

  step(): void {
    parallel {
      task {
        this.a = this.a * 2;
      }
      task {
        this.b = this.b * 3;
      }
    }
  }

  sum(): int {
    return this.a + this.b;
  }
}

function main(): int {
  const s: Sim = new Sim();
  for (let k = 0; k < 4; k += 1) {
    s.step();
  }
  println(s.a);
  println(s.b);
  println(s.sum());
  return 0;
}
""", "16\n81\n97\n", comment="`this` is a shared read-only binding inside tasks; this.a and this.b are distinct object fields.")

run("024_contextual_keywords", """function parallel(x: int): int {
  return x + 1;
}

function main(): int {
  let task: int = 3;
  let out: int = 0;
  task = parallel(task);
  println(task);
  parallel {
    task {
      out = task * 2;
    }
    task {
      println(parallel(task));
    }
  }
  println(out);
  return 0;
}
""", "4\n5\n8\n", comment="`parallel` / `task` are contextual (3.4): still legal identifiers; syntax only at statement start followed by `{`.")

run("025_alias_different_fields", """class Stats {
  lo: int;
  hi: int;

  constructor() {
    this.lo = 0;
    this.hi = 0;
  }
}

function main(): int {
  const p: Stats = new Stats();
  const q: Stats = p;
  parallel {
    task {
      p.lo = 1;
    }
    task {
      q.hi = 2;
    }
  }
  println(p.lo);
  println(p.hi);
  return 0;
}
""", "1\n2\n", repeat=10, threads="1,2",
    comment="q is an alias of p (same origin, 5.3) but the fields differ: Disjoint.")

run("026_task_allocates_objects", COUNTER + """
function main(): int {
  let cs: Counter[2] = [new Counter(0), new Counter(0)];
  parallel {
    task {
      let t: Counter = new Counter(1);
      t.inc();
      t.inc();
      cs[0] = t;
    }
    task {
      let t: Counter = new Counter(10);
      t.inc();
      cs[1] = t;
    }
  }
  println(cs[0].get());
  println(cs[1].get());
  return 0;
}
""", "3\n11\n", repeat=10, threads="1,2",
    comment="Objects allocated inside a task are task-private until published through a (checked) write (4.8, 5.3).")

# ---- traps
trap("027_trap_lowest_task_index", """function main(): int {
  let z: int = 0;
  let r: int[2] = [0, 0];
  println("start");
  parallel {
    task {
      println("A");
      r[0] = 1;
    }
    task {
      println("B");
      r[1] = 10 / z;
      println("B2");
    }
    task {
      println("C");
      r[5] = 1;
    }
  }
  println("unreachable");
  return 0;
}
""", 101, "division by zero", stdout="start\nA\nB\n", repeat=20, threads="1,2,4",
     comment="Task 2 (101) and task 3 (OOB 102) both trap; lowest index wins. Stdout = output before the block + task 1 in full + task 2 up to the trap; task 3 dropped (10.2).")

trap("028_trap_single_task_oob", """function main(): int {
  let k: int = 2;
  parallel {
    task {
      println("ok");
    }
    task {
      let a: int[2] = [1, 2];
      println(a[k]);
    }
  }
  return 0;
}
""", 102, "index out of bounds", stdout="ok\n", repeat=10, threads="1,2",
     comment="Existing traps 101-108 pass through unchanged from tasks (10.2, 13).")

run("029_parallel_recursion", """function fib(n: int): int {
  if (n < 2) {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

function pfib(n: int): int {
  if (n < 12) {
    return fib(n);
  }
  let a: int = 0;
  let b: int = 0;
  parallel {
    task {
      a = pfib(n - 1);
    }
    task {
      b = pfib(n - 2);
    }
  }
  return a + b;
}

function main(): int {
  println(pfib(22));
  return 0;
}
""", "17711\n", repeat=5, threads="1,2,4,8",
    comment="Recursive fork/join. MUST NOT deadlock for any worker count, including 1 (11.3). `return` is legal outside task bodies.")

# =========================================================== CLASS 2: same-value WARNING (compile ok, exit 0)
runw("030_warn_same_value", two_task("      a = 1;\n", "      a = 1;\n"), "1\n",
     [("W0801", A("a = 1;", 2), A("a = 1;", 1))],
     comment="The PLAN example with EQUAL values: warning, compile succeeds, program runs (6.2).")

runw("088_warn_three_tasks_same_value", """function main(): int {
  let a: int = 0;
  parallel {
    task {
      a = 7;
    }
    task {
      a = 7;
    }
    task {
      a = 7;
    }
  }
  println(a);
  return 0;
}
""", "7\n", [("W0801", A("a = 7;", 2), A("a = 7;", 1)), ("W0801", A("a = 7;", 3), A("a = 7;", 1))],
     comment="More than two tasks: one diagnostic per statement of the later task, paired with the lowest-numbered conflicting earlier task (6.10).",
     repeat=5, threads="1,4")

runw("090_warn_const_folded", """function main(): int {
  const K: int = 3;
  let a: int = 0;
  parallel {
    task {
      a = 1 + 2;
    }
    task {
      a = K;
    }
  }
  println(a);
  return 0;
}
""", "3\n", [("W0801", A("a = K;", 1), A("a = 1 + 2;", 1))],
     comment="`1 + 2` and a const bound to 3 both fold to int 3 (5.6): equal statically-known values.")

runw("091_warn_struct_field_same_value", PAIR + """
function main(): int {
  let p: Pair = new Pair(0, 0);
  parallel {
    task {
      p.left = 5;
    }
    task {
      p.left = 5;
      p.right = 6;
    }
  }
  println(p.left);
  println(p.right);
  return 0;
}
""", "5\n6\n", [("W0801", A("p.left = 5;", 2), A("p.left = 5;", 1))],
     comment="Same field, same constant: warning; the extra write p.right in task 2 conflicts with nothing.")

runw("092_warn_interprocedural_same_const", FLAG + """
function setOne(f: Flag): void {
  f.on = 1;
}

function main(): int {
  const f: Flag = new Flag();
  parallel {
    task {
      setOne(f);
    }
    task {
      setOne(f);
    }
  }
  println(f.on);
  return 0;
}
""", "1\n", [("W0801", A("setOne(f);", 2), A("setOne(f);", 1))],
     comment="Summary of setOne = {write Formal0.on := Const 1} (7.3). Both call sites instantiate it on the same object with the same constant: warning at the call.")

runw("095_warn_float_same_value", """function main(): int {
  let x: float = 0.0;
  parallel {
    task {
      x = 0.5;
    }
    task {
      x = 0.5;
    }
  }
  println(x);
  return 0;
}
""", "0.5\n", [("W0801", A("x = 0.5;", 2), A("x = 0.5;", 1))], comment="Floats compare by IEEE bit pattern (5.6).")

runw("097_warn_string_same_value", """function main(): int {
  let s: string = "";
  parallel {
    task {
      s = "hi" + "!";
    }
    task {
      s = "hi!";
    }
  }
  println(s);
  return 0;
}
""", "hi!\n", [("W0801", A('s = "hi!";', 1), A('s = "hi" + "!";', 1))], comment="Constant string concatenation folds (5.6).")

runw("066_warn_dynamic_index_same_value", """function main(): int {
  let a: int[4] = [0, 0, 0, 0];
  let i: int = 1;
  let j: int = 1;
  parallel {
    task {
      a[i] = 1;
    }
    task {
      a[j] = 1;
    }
  }
  println(a[1]);
  return 0;
}
""", "1\n", [("W0801", A("a[j] = 1;", 1), A("a[i] = 1;", 1))],
     comment="Possible overlap (i, j unknown), but both write the same constant: warning ('may write'), never an error (6.2). Both values equal, so the result is the same whether or not i == j.",
     repeat=10, threads="1,2")

# =========================================================== CLASS 3: different / unknown values -> ERROR E0801
cerr("031_conflict_ww_diff_value", two_task("      a = 1;\n", "      a = 2;\n"), "E0801", A("a = 2;"), A("a = 1;"),
     comment="The PLAN.md example a=1 vs a=2 (6.3). Primary = access in the higher-numbered task, note = the earlier one.")
cerr("033_conflict_compound", two_task("      a += 1;\n", "      a += 1;\n"), "E0801", A("a += 1;", 2), A("a += 1;", 1),
     comment="Compound assignment = read+write of a value that depends on the read: never a known constant, so write/write with unknown values (6.5).")
cerr("101_conflict_const_vs_unknown", two_task("      a = 1;\n", "      a = y;\n", decl="  let a: int = 0;\n  let y: int = 1;\n"), "E0801", A("a = y;"), A("a = 1;"),
     comment="A constant against a non-constant is 'not provably equal' -> error (OQ-M35-02 option A, 6.3).")
cerr("068_conflict_call_result_values", """function f(): int {
  return 1;
}

function main(): int {
  let a: int = 0;
  parallel {
    task {
      a = f();
    }
    task {
      a = f();
    }
  }
  println(a);
  return 0;
}
""", "E0801", A("a = f();", 2), A("a = f();", 1), comment="Calls are never constant expressions, even f() that obviously returns 1 (5.6): a = f() vs a = f() is an error under option A.")
cerr("069_conflict_nonconst_same_expr", two_task("      a = x;\n", "      a = x;\n", decl="  let a: int = 0;\n  let x: int = 5;\n"), "E0801", A("a = x;", 2), A("a = x;", 1),
     comment="`let x` is not a constant expression (no constant propagation, 5.6): the same non-constant expression twice is an error.")
cerr("096_conflict_float_signed_zero", """function main(): int {
  let x: float = 1.0;
  parallel {
    task {
      x = 0.0;
    }
    task {
      x = -0.0;
    }
  }
  println(x);
  return 0;
}
""", "E0801", A("x = -0.0;"), A("x = 0.0;"), comment="0.0 and -0.0 compare equal in IEEE but have different bit patterns: different statically-known values (5.6).")

s = PAIR.replace("Pair", "Point").replace("left", "x").replace("right", "y") + """
function main(): int {
  let p: Point = new Point(0, 0);
  parallel {
    task {
      p.x = 1;
    }
    task {
      p.x = 2;
    }
  }
  println(p.x);
  return 0;
}
"""
cerr("034_conflict_struct_field", s, "E0801", A("p.x = 2;"), A("p.x = 1;"))
cerr("035_conflict_const_index", """function main(): int {
  const k: int = 0;
  let r: int[2] = [0, 0];
  parallel {
    task {
      r[0] = 1;
    }
    task {
      r[k] = 2;
    }
  }
  println(r[0]);
  return 0;
}
""", "E0801", A("r[k] = 2;"), A("r[0] = 1;"), comment="`k` is a const bound to an integer literal: constant index 0 (5.4).")
cerr("036_conflict_whole_vs_field", PAIR + """
function main(): int {
  let p: Pair = new Pair(1, 2);
  parallel {
    task {
      p = new Pair(1, 2);
    }
    task {
      p.left = 5;
    }
  }
  println(p.left);
  return 0;
}
""", "E0801", A("p.left = 5;"), A("p = new Pair(1, 2);"), comment="A whole-value write overlaps every sub-path (prefix rule, 5.5).")
cerr("037_conflict_class_field", COUNTER_MIN + """
function main(): int {
  const c: Counter = new Counter(0);
  parallel {
    task {
      c.n = 1;
    }
    task {
      c.n = 2;
    }
  }
  println(c.n);
  return 0;
}
""", "E0801", A("c.n = 2;"), A("c.n = 1;"))
cerr("038_conflict_via_function_summary", COUNTER_MIN + """
function bump(c: Counter): void {
  c.n = c.n + 1;
}

function main(): int {
  const c: Counter = new Counter(0);
  parallel {
    task {
      bump(c);
    }
    task {
      bump(c);
    }
  }
  println(c.n);
  return 0;
}
""", "E0801", A("bump(c);", 2), A("bump(c);", 1),
     comment="Summary of bump = {read+write Formal0.n := Unknown} (7.1); instantiated on the same object twice: error at the call (position = start of call expression).")
cerr("039_conflict_push_both", """function main(): int {
  let xs: int[] = [];
  parallel {
    task {
      push(xs, 1);
    }
    task {
      push(xs, 2);
    }
  }
  println(len(xs));
  return 0;
}
""", "E0801", A("push(xs, 2);"), A("push(xs, 1);"), comment="push writes the whole array with an unknown value, even push(xs,1) twice (6.7).")
cerr("044_conflict_same_renderer", """import { Scene, PerspectiveCamera, Renderer } from "farmos:scene";

function main(): int {
  const sceneA: Scene = new Scene();
  const sceneB: Scene = new Scene();
  const cameraA: PerspectiveCamera = new PerspectiveCamera(50, 1, 0.1, 100);
  const cameraB: PerspectiveCamera = new PerspectiveCamera(50, 1, 0.1, 100);
  const renderer: Renderer = new Renderer(16, 16);
  parallel {
    task {
      renderer.render(sceneA, cameraA);
    }
    task {
      renderer.render(sceneB, cameraB);
    }
  }
  return 0;
}
""", "E0801", A("renderer.render(sceneB"), A("renderer.render(sceneA"), comment="render writes renderer.#frame (8.3): the same renderer from two tasks is a conflict.")
cerr("045_conflict_same_scene", """import { Scene, PerspectiveCamera, Renderer } from "farmos:scene";

function main(): int {
  const scene: Scene = new Scene();
  const cam1: PerspectiveCamera = new PerspectiveCamera(50, 1, 0.1, 100);
  const cam2: PerspectiveCamera = new PerspectiveCamera(50, 1, 0.1, 100);
  const r1: Renderer = new Renderer(16, 16);
  const r2: Renderer = new Renderer(16, 16);
  parallel {
    task {
      r1.render(scene, cam1);
    }
    task {
      r2.render(scene, cam2);
    }
  }
  return 0;
}
""", "E0801", A("r2.render(scene"), A("r1.render(scene"), comment="render writes matrix/matrixWorld of every node of the scene tree (M3 4.5; 8.3). Same scene, different renderers and cameras: still E0801 (write/write outranks the read/write also present at that call).")
cerr("046_conflict_nested_inner", """function main(): int {
  let a: int = 0;
  parallel {
    task {
      parallel {
        task {
          a = 1;
        }
        task {
          a = 2;
        }
      }
    }
    task {
      println(3);
    }
  }
  return 0;
}
""", "E0801", A("a = 2;"), A("a = 1;"), comment="Conflict inside the inner block (6.9); the outer block has none.")
cerr("047_conflict_through_nested", """function main(): int {
  let a: int = 0;
  let b: int = 0;
  parallel {
    task {
      parallel {
        task {
          a = 1;
        }
        task {
          b = 2;
        }
      }
    }
    task {
      a = 3;
    }
  }
  return 0;
}
""", "E0801", A("a = 3;"), A("a = 1;"), comment="Accesses of a nested block count as accesses of the enclosing outer task (6.9).")
cerr("053_conflict_untaken_branch", """function main(): int {
  let a: int = 0;
  let flag: bool = false;
  parallel {
    task {
      if (flag) {
        a = 1;
      }
    }
    task {
      a = 2;
    }
  }
  println(a);
  return 0;
}
""", "E0801", A("a = 2;"), A("a = 1;"), comment="Analysis is flow-insensitive (6.8): a write that never executes still counts.")
cerr("058_conflict_this_field", """class Box {
  n: int;

  constructor() {
    this.n = 0;
  }

  run(): void {
    parallel {
      task {
        this.n = 1;
      }
      task {
        this.n = 2;
      }
    }
  }
}

function main(): int {
  const b: Box = new Box();
  b.run();
  println(b.n);
  return 0;
}
""", "E0801", A("this.n = 2;"), A("this.n = 1;"), comment="`this` is one shared read-only binding inside tasks; this.n is one object field.")
cerr("059_conflict_dynamic_index_loop", """function main(): int {
  let a: int[4] = [0, 0, 0, 0];
  for (let round = 0; round < 3; round += 1) {
    let i: int = round;
    let j: int = 2 - round;
    parallel {
      task {
        a[i] = 1;
      }
      task {
        a[j] = 2;
      }
    }
    println(round);
  }
  return 0;
}
""", "E0801", A("a[j] = 2;"), A("a[i] = 1;"), comment="Indices from `let` bindings have no interval (5.4): possible overlap + different values = error, even though this program would only clash at round 1.")
cerr("060_conflict_dynamic_index_lets", """function main(): int {
  let a: int[4] = [0, 0, 0, 0];
  let i: int = 1;
  let j: int = 1;
  parallel {
    task {
      a[i] = 10;
    }
    task {
      a[j] = 20;
    }
  }
  println(a[1]);
  return 0;
}
""", "E0801", A("a[j] = 20;"), A("a[i] = 10;"), comment="No constant propagation through `let` (5.4): a[i] vs a[j] may overlap -> error (OQ-M35-02 A). Contrast 066 (equal constants -> warning).")
cerr("061_conflict_alias_class", COUNTER_MIN + """
function main(): int {
  const p: Counter = new Counter(0);
  const q: Counter = p;
  parallel {
    task {
      p.n = 1;
    }
    task {
      q.n = 2;
    }
  }
  println(p.n);
  return 0;
}
""", "E0801", A("q.n = 2;"), A("p.n = 1;"), comment="q aliases p (same origin, 5.3): a DEFINITE same-object conflict found statically.")
cerr("067_conflict_unknown_alias_params", COUNTER_MIN + """
function bumpBoth(x: Counter, y: Counter): void {
  parallel {
    task {
      x.n = x.n + 1;
    }
    task {
      y.n = y.n + 1;
    }
  }
}

function main(): int {
  const a: Counter = new Counter(0);
  const b: Counter = new Counter(0);
  bumpBoth(a, b);
  println(a.n);
  return 0;
}
""", "E0801", A("y.n = y.n + 1;"), A("x.n = x.n + 1;"),
     comment="x and y are opaque parameters of the same class: the callee cannot know they differ -> 'may alias' error (5.3, OQ-M35-02 A), although this caller passes distinct objects.")
cerr("085_conflict_same_index_binding", """function main(): int {
  let a: int[4] = [0, 0, 0, 0];
  let i: int = 2;
  parallel {
    task {
      a[i] = 1;
    }
    task {
      a[i] = 2;
    }
  }
  println(a[2]);
  return 0;
}
""", "E0801", A("a[i] = 2;"), A("a[i] = 1;"), comment="The same outer identifier as index in both tasks is statically the same index: DEFINITE conflict (5.4).")
cerr("086_conflict_const_arith_index", """function main(): int {
  const k: int = 1;
  let r: int[4] = [0, 0, 0, 0];
  parallel {
    task {
      r[k + 1] = 1;
    }
    task {
      r[2] = 2;
    }
  }
  println(r[2]);
  return 0;
}
""", "E0801", A("r[2] = 2;"), A("r[k + 1] = 1;"), comment="Constant index folding covers + - * over consts: r[k + 1] is r[2] (5.4).")
cerr("093_conflict_interprocedural_diff_const", FLAG + """
function setOne(f: Flag): void {
  f.on = 1;
}

function setTwo(f: Flag): void {
  f.on = 2;
}

function main(): int {
  const f: Flag = new Flag();
  parallel {
    task {
      setOne(f);
    }
    task {
      setTwo(f);
    }
  }
  println(f.on);
  return 0;
}
""", "E0801", A("setTwo(f);"), A("setOne(f);"), comment="Summaries carry constants (Const 1 vs Const 2): different statically-known values -> error (7.3).")
cerr("094_conflict_no_const_propagation", FLAG + """
function set(f: Flag, v: int): void {
  f.on = v;
}

function main(): int {
  const f: Flag = new Flag();
  parallel {
    task {
      set(f, 1);
    }
    task {
      set(f, 1);
    }
  }
  println(f.on);
  return 0;
}
""", "E0801", A("set(f, 1);", 2), A("set(f, 1);", 1), comment="No constant propagation into callees: the summary writes Unknown (the parameter v), so even set(f,1) twice is an error (7.3). Contrast 092.")
cerr("076_conflict_struct_method", """struct Acc {
  total: int;

  add(v: int): void {
    this.total = this.total + v;
  }
}

function main(): int {
  let a: Acc = new Acc(0);
  parallel {
    task {
      a.add(1);
    }
    task {
      a.add(2);
    }
  }
  println(a.total);
  return 0;
}
""", "E0801", A("a.add(2);"), A("a.add(1);"), comment="Struct methods take `this` by reference (M2 3.2): effect lands on receiver path a.total (7.1).")
cerr("077_conflict_rotation_quaternion_sync", SCENE_IMP + f"""
function main(): int {{
  const cube: Mesh = {MESH};
  parallel {{
    task {{
      cube.rotation.x = 0.5;
    }}
    task {{
      cube.quaternion.w = 1;
    }}
  }}
  println(cube.rotation.x);
  return 0;
}}
""", "E0801", A("cube.quaternion.w = 1;"), A("cube.rotation.x = 0.5;"), comment="M3 4.2 sync: writing rotation also writes quaternion and vice versa (8.3): both sides write both fields with derived (unknown) values.")
cerr("098_conflict_hierarchy_add", """import { Scene, Object3D } from "farmos:scene";

function main(): int {
  const s1: Scene = new Scene();
  const s2: Scene = new Scene();
  const a: Object3D = new Object3D();
  const b: Object3D = new Object3D();
  parallel {
    task {
      s1.add(a);
    }
    task {
      s2.add(b);
    }
  }
  return 0;
}
""", "E0801", A("s2.add(b);"), A("s1.add(a);"), comment="add/remove/addAt write the abstract location #graph (8.3) because add also detaches the child from its previous parent: any two hierarchy mutations in different tasks conflict, even on different scenes.")
cerr("104_conflict_vector_mutating_method", """import { Vector3 } from "farmos:math";

function main(): int {
  let v: Vector3 = new Vector3(1.0, 2.0, 3.0);
  parallel {
    task {
      v.add(new Vector3(1.0, 1.0, 1.0));
    }
    task {
      v.multiplyScalar(2.0);
    }
  }
  println(v);
  return 0;
}
""", "E0801", A("v.multiplyScalar(2.0);"), A("v.add(new Vector3"), comment="Mutating math methods write the whole receiver with an unknown value (8.2).")

# =========================================================== CLASS 4: read/write ERROR E0802
cerr("032_conflict_rw", two_task("      b = a + 1;\n", "      a = 2;\n", decl="  let a: int = 0;\n  let b: int = 0;\n", tail="  println(a);\n  println(b);\n"),
     "E0802", A("a = 2;"), A("a + 1"), comment="Task 1 reads a, task 2 writes a (6.4). Note = the read.")
cerr("054_conflict_method_rw", COUNTER + """
function main(): int {
  const c: Counter = new Counter(0);
  parallel {
    task {
      c.inc();
    }
    task {
      println(c.get());
    }
  }
  return 0;
}
""", "E0802", A("c.get()"), A("c.inc()"), comment="Summaries: inc reads+writes this.n; get reads this.n (7.1). Read of c.n in task 2 vs write in task 1. Position = start of call expression.")
cerr("062_conflict_dynamic_rw", """function main(): int {
  let a: int[4] = [1, 2, 3, 4];
  let i: int = 2;
  let j: int = 2;
  let out: int = 0;
  parallel {
    task {
      out = a[i];
    }
    task {
      a[j] = 99;
    }
  }
  println(out);
  return 0;
}
""", "E0802", A("a[j] = 99;"), A("a[i]"), comment="Possible read/write overlap through dynamic indices: error (6.4, 5.4).")
cerr("065_conflict_three_tasks_rw", """function main(): int {
  let a: int[3] = [1, 2, 3];
  let i: int = 0;
  let j: int = 0;
  let k: int = 0;
  let o1: int = 0;
  let o2: int = 0;
  parallel {
    task {
      o1 = a[i];
    }
    task {
      o2 = a[j];
    }
    task {
      a[k] = 9;
    }
  }
  println(o1 + o2);
  return 0;
}
""", "E0802", A("a[k] = 9;"), A("a[i]"), comment="Tasks 1 and 2 only read (no conflict between them). Task 3's write is reported once, against the LOWEST conflicting earlier task (task 1) (6.10).")
cerr("081_conflict_push_vs_len", """function main(): int {
  let xs: int[] = [1, 2, 3];
  let n: int = 0;
  parallel {
    task {
      push(xs, 4);
    }
    task {
      n = len(xs);
    }
  }
  println(n);
  return 0;
}
""", "E0802", A("len(xs)"), A("push(xs, 4)"), comment="push writes the whole array (incl. its length); len(xs) reads xs.#len: prefix overlap (6.7, 5.1).")
cerr("083_conflict_push_vs_index_read", """function main(): int {
  let xs: int[] = [1, 2, 3];
  let i: int = 0;
  let out: int = 0;
  parallel {
    task {
      push(xs, 4);
    }
    task {
      out = xs[i];
    }
  }
  println(out);
  return 0;
}
""", "E0802", A("xs[i]"), A("push(xs, 4)"), comment="Element access implicitly reads xs.#len (bounds check, 5.2); push may reallocate storage: no dynamic index can hide it.")
cerr("087_conflict_index_variable_written", """function main(): int {
  let a: int[4] = [0, 0, 0, 0];
  let i: int = 1;
  parallel {
    task {
      a[i] = 1;
    }
    task {
      i = 3;
    }
  }
  println(a[1]);
  return 0;
}
""", "E0802", A("i = 3;"), A("a[i] = 1;", 1, 2), comment="Evaluating an index reads the variable (5.2): task 1 reads i, task 2 writes i. Note = the `i` inside the index.")
cerr("073_conflict_render_vs_move", """import { Scene, PerspectiveCamera, Mesh, BoxGeometry, MeshBasicMaterial, Renderer } from "farmos:scene";

function main(): int {
  const scene: Scene = new Scene();
  const camera: PerspectiveCamera = new PerspectiveCamera(50, 1, 0.1, 100);
  const mesh: Mesh = new Mesh(new BoxGeometry(1, 1, 1), new MeshBasicMaterial(0xff0000));
  scene.add(mesh);
  const renderer: Renderer = new Renderer(16, 16);
  parallel {
    task {
      renderer.render(scene, camera);
    }
    task {
      mesh.position.x = 5;
    }
  }
  println("done");
  return 0;
}
""", "E0802", A("mesh.position.x = 5;"), A("renderer.render(scene"), comment="render reads every node field of the scene tree; the checker cannot know `mesh` is a child, so it MAY be in the tree: 'may' error (8.3, 5.3).")
cerr("074_conflict_same_camera", """import { Scene, PerspectiveCamera, Renderer } from "farmos:scene";

function main(): int {
  const s1: Scene = new Scene();
  const s2: Scene = new Scene();
  const cam: PerspectiveCamera = new PerspectiveCamera(50, 1, 0.1, 100);
  const r1: Renderer = new Renderer(16, 16);
  const r2: Renderer = new Renderer(16, 16);
  parallel {
    task {
      r1.render(s1, cam);
    }
    task {
      r2.render(s2, cam);
    }
  }
  return 0;
}
""", "E0801", A("r2.render(s2, cam)"), A("r1.render(s1, cam)"), comment="render writes the camera's matrixWorld / matrixWorldInverse / projectionMatrix (8.3).")
cerr("106_conflict_alias_scene", """import { Scene, PerspectiveCamera, Renderer } from "farmos:scene";

function main(): int {
  const scene: Scene = new Scene();
  const s1: Scene = scene;
  const s2: Scene = scene;
  const cam1: PerspectiveCamera = new PerspectiveCamera(50, 1, 0.1, 100);
  const cam2: PerspectiveCamera = new PerspectiveCamera(50, 1, 0.1, 100);
  const r1: Renderer = new Renderer(16, 16);
  const r2: Renderer = new Renderer(16, 16);
  parallel {
    task {
      r1.render(s1, cam1);
    }
    task {
      r2.render(s2, cam2);
    }
  }
  println("done");
  return 0;
}
""", "E0801", A("r2.render(s2, cam2)"), A("r1.render(s1, cam1)"), comment="Aliased scene through two bindings (same origin, 5.3): definite conflict, found statically.")

# =========================================================== multi-diagnostic / ordering
src = """function main(): int {
  let a: int = 0;
  let b: int = 0;
  parallel {
    task {
      a = 1;
      b = 1;
    }
    task {
      b = 2;
      a = 2;
    }
  }
  println(a + b);
  return 0;
}
"""
fx("063_multi_diag_source_order", src, "compile_error", 1,
   diags=[("error", "E0801", A("b = 2;"), A("b = 1;")), ("error", "E0801", A("a = 2;"), A("a = 1;"))],
   exact=True, repeat=5,
   comment="Two independent conflicts: reported in primary-source-position order (b = 2 is earlier in the file than a = 2), not by variable name or task-internal access order (6.10, 12.4). repeat = compiler output byte-identical every time.")

src = """function main(): int {
  let a: int = 0;
  let b: int = 0;
  parallel {
    task {
      a = 1;
    }
    task {
      a = 1;
    }
  }
  parallel {
    task {
      b = 1;
    }
    task {
      b = 2;
    }
  }
  return 0;
}
"""
fx("064_warning_then_error_order", src, "compile_error", 1,
   diags=[("warning", "W0801", A("a = 1;", 2), A("a = 1;", 1)), ("error", "E0801", A("b = 2;"), A("b = 1;"))],
   exact=True, repeat=5,
   comment="Warnings and errors share one source-ordered stream (12.4); a warning does not suppress or reorder the later error.")

fx("089_mixed_three_tasks", """function main(): int {
  let a: int = 0;
  parallel {
    task {
      a = 1;
    }
    task {
      a = 1;
    }
    task {
      a = 2;
    }
  }
  println(a);
  return 0;
}
""", "compile_error", 1,
   diags=[("warning", "W0801", A("a = 1;", 2), A("a = 1;", 1)), ("error", "E0801", A("a = 2;"), A("a = 1;", 1))],
   exact=True, comment="Task 2 vs task 1: equal constants -> warning. Task 3 conflicts with both earlier tasks with a DIFFERENT value: one error, paired with the lowest task (task 1) (6.10).")

fx("103_error_outranks_warning_pair", """function main(): int {
  let a: int = 0;
  let b: int = 0;
  parallel {
    task {
      a = 1;
    }
    task {
      a = 1;
    }
    task {
      b = a;
    }
  }
  println(b);
  return 0;
}
""", "compile_error", 1,
   diags=[("warning", "W0801", A("a = 1;", 2), A("a = 1;", 1)), ("error", "E0802", A("b = a;", 1, 4), A("a = 1;", 1))],
   exact=True, comment="Equal-value writes stay a warning; the third task's READ of a is an independent error (a read never inherits the same-value leniency, 6.4).")

fx("100_werror_promotes_warning", two_task("      a = 1;\n", "      a = 1;\n"), "compile_error", 1,
   diags=[("error", "W0801", A("a = 1;", 2), A("a = 1;", 1))], flags="--werror", exact=True,
   comment="With --werror the warning is reported with severity `error` (code unchanged) and farmc exits 1 (12.3).")

# =========================================================== structural / control-flow errors
cerr("040_return_in_task", """function main(): int {
  let a: int = 0;
  parallel {
    task {
      a = 1;
      return 0;
    }
    task {
      println(2);
    }
  }
  return 0;
}
""", "E0803", A("return 0;"))
cerr("041_break_in_task", """function main(): int {
  for (let i = 0; i < 3; i += 1) {
    parallel {
      task {
        break;
      }
      task {
        println(i);
      }
    }
  }
  return 0;
}
""", "E0804", A("break;"), comment="The nearest enclosing loop-or-task boundary of `break` is a task: E0804 (not E0510, although a loop exists outside) (10.1).")
cerr("056_continue_in_task", """function main(): int {
  let i: int = 0;
  while (i < 3) {
    i += 1;
    parallel {
      task {
        continue;
      }
      task {
        println(i);
      }
    }
  }
  return 0;
}
""", "E0804", A("continue;"))
cerr("042_task_outside_parallel", """function main(): int {
  task {
    println(1);
  }
  return 0;
}
""", "E0806", A("task {"))
cerr("055_task_in_task", """function main(): int {
  parallel {
    task {
      task {
        println(1);
      }
    }
  }
  return 0;
}
""", "E0806", A("task {", 2), comment="`task` items are legal only directly inside a `parallel` block (3.5).")
cerr("043_file_io_in_task", """import { Renderer } from "farmos:scene";

function main(): int {
  const renderer: Renderer = new Renderer(8, 8);
  parallel {
    task {
      renderer.savePNG("a.png");
    }
    task {
      println(1);
    }
  }
  return 0;
}
""", "E0805", A("renderer.savePNG"), comment="File output is forbidden inside tasks in M3.5 (7.5, OQ-M35-03).")
cerr("057_io_via_function", """import { Renderer } from "farmos:scene";

function saveIt(r: Renderer): void {
  r.savePNG("x.png");
}

function main(): int {
  const renderer: Renderer = new Renderer(8, 8);
  parallel {
    task {
      saveIt(renderer);
    }
    task {
      println(1);
    }
  }
  return 0;
}
""", "E0805", A("saveIt(renderer);"), comment="The file-output flag is part of the function summary (7.5): reported at the call inside the task.")
cerr("048_empty_parallel", """function main(): int {
  parallel {
  }
  return 0;
}
""", "E0202", A("}", 1), comment="`parallel` requires at least one `task` (3.3): the closing brace is unexpected.")
cerr("051_stmt_in_parallel_not_task", """function main(): int {
  parallel {
    let x: int = 1;
  }
  return 0;
}
""", "E0202", A("let x", 1), comment="Only `task { ... }` items may appear directly inside `parallel { ... }` (3.3).")
cerr("049_const_assign_in_task", """function main(): int {
  const k: int = 1;
  let a: int = 0;
  parallel {
    task {
      k = 2;
    }
    task {
      a = 3;
    }
  }
  return 0;
}
""", "E0504", A("k = 2;"), comment="M1 rules apply unchanged inside tasks; conflict analysis does not run after checker errors (12.5).")
cerr("050_task_local_not_visible", """function main(): int {
  parallel {
    task {
      let t: int = 1;
      println(t);
    }
    task {
      println(t);
    }
  }
  return 0;
}
""", "E0505", A("println(t);", 2, 8), comment="A task's locals are invisible to sibling tasks (4.3).")
n_tasks = 257
cerr("052_too_many_tasks", "function main(): int {\n  parallel {\n" + "    task {}\n" * n_tasks + "  }\n  return 0;\n}\n",
     "E0807", A("task {}", n_tasks), comment="Limit: 256 tasks per parallel block; the 257th is reported (3.3).")

# =========================================================== multi-file
helper = """export class Counter {
  n: int;

  constructor(n: int) {
    this.n = n;
  }
}

export function bump(c: Counter): void {
  c.n = c.n + 1;
}
"""
main_bad = """import { Counter, bump } from "./helper.fm";

function main(): int {
  const c: Counter = new Counter(0);
  parallel {
    task {
      bump(c);
    }
    task {
      bump(c);
    }
  }
  println(c.n);
  return 0;
}
"""
fx("070_module_summary_conflict", None, "compile_error", 1,
   diags=[("error", "E0801", A("bump(c);", 2), A("bump(c);", 1))], files={"main.fm": main_bad, "helper.fm": helper},
   comment="Function summaries cross module boundaries (7.4).")
main_ok = """import { Counter, bump } from "./helper.fm";

function main(): int {
  const a: Counter = new Counter(0);
  const b: Counter = new Counter(10);
  parallel {
    task {
      bump(a);
      bump(a);
    }
    task {
      bump(b);
    }
  }
  println(a.n);
  println(b.n);
  return 0;
}
"""
fx("071_module_summary_ok", None, "run", 0, stdout="2\n11\n", repeat=10, files={"main.fm": main_ok, "helper.fm": helper})

# =========================================================== more run fixtures
run("072_dynamic_shared_reads", """function main(): int {
  let a: int[4] = [5, 6, 7, 8];
  let i: int = 2;
  let j: int = 2;
  let o1: int = 0;
  let o2: int = 0;
  parallel {
    task {
      o1 = a[i];
    }
    task {
      o2 = a[j] * 2;
    }
  }
  println(o1);
  println(o2);
  return 0;
}
""", "7\n14\n", repeat=10, threads="1,2", comment="Read/read is never a conflict, however the indices are computed (6.1 class 1).")
run("075_shadow_private", """function main(): int {
  let a: int = 1;
  let out: int = 0;
  parallel {
    task {
      let a: int = 10;
      a = a + 1;
      out = a;
    }
    task {
      let a: int = 20;
      a += 1;
      println(a);
    }
  }
  println(a);
  println(out);
  return 0;
}
""", "21\n1\n11\n", comment="Each task declares its own `a`: writes go to task-private locals, the outer `a` is untouched.")
run("078_disjoint_position_axes", SCENE_IMP + f"""
function main(): int {{
  const cube: Mesh = {MESH};
  parallel {{
    task {{
      cube.position.x = 1;
    }}
    task {{
      cube.position.y = 2;
    }}
    task {{
      cube.scale.z = 3;
    }}
  }}
  println(cube.position.x);
  println(cube.position.y);
  println(cube.position.z);
  println(cube.scale.z);
  return 0;
}}
""", "1\n2\n0\n3\n", comment="Field granularity down to leaf components of inline struct fields: position.x / position.y / scale.z are three locations of one object (5.1, 8.3).")
run("079_two_meshes_move", SCENE_IMP + f"""
function main(): int {{
  const m1: Mesh = {MESH};
  const m2: Mesh = {MESH};
  parallel {{
    task {{
      m1.position.x = 1;
    }}
    task {{
      m2.position.x = 2;
    }}
  }}
  println(m1.position.x);
  println(m2.position.x);
  return 0;
}}
""", "1\n2\n", repeat=10, threads="1,2", comment="Two `new` sites: provably different objects (5.3).")
run("080_loop_break_inside_task", """function main(): int {
  let found: int = -1;
  let evens: int = 0;
  parallel {
    task {
      for (let i = 0; i < 100; i += 1) {
        if (i * i > 50) {
          found = i;
          break;
        }
      }
    }
    task {
      for (let i = 0; i < 10; i += 1) {
        if (i % 2 == 1) {
          continue;
        }
        evens += 1;
      }
    }
  }
  println(found);
  println(evens);
  return 0;
}
""", "8\n5\n", comment="break/continue are fine when their target loop is inside the same task (contrast 041, 056).")
run("082_dynamic_array_shared_read", """function main(): int {
  let xs: int[] = [];
  for (let i = 0; i < 6; i += 1) {
    push(xs, i * i);
  }
  let lo: int = 0;
  let hi: int = 0;
  parallel {
    task {
      for (let i = 0; i < len(xs) / 2; i += 1) {
        lo += xs[i];
      }
    }
    task {
      for (let i = len(xs) / 2; i < len(xs); i += 1) {
        hi += xs[i];
      }
    }
  }
  println(lo);
  println(hi);
  return 0;
}
""", "5\n50\n", repeat=10, threads="1,2", comment="Both tasks read xs[*] and len(xs); nobody writes xs.")
run("084_global_const_reads", """const SCALE: int = 3;
const TABLE: int[3] = [10, 20, 30];

function main(): int {
  let a: int = 0;
  let b: int = 0;
  parallel {
    task {
      a = TABLE[0] * SCALE;
    }
    task {
      b = TABLE[2] * SCALE + TABLE[1];
    }
  }
  println(a);
  println(b);
  return 0;
}
""", "30\n110\n", comment="Top-level consts are the only globals (M1 3.1) and are immutable: always readable from tasks (4.2).")
run("102_method_reads_only", COUNTER + """
function main(): int {
  const c: Counter = new Counter(41);
  let x: int = 0;
  let y: int = 0;
  parallel {
    task {
      x = c.get() + 1;
    }
    task {
      y = c.get() + 2;
    }
  }
  println(x);
  println(y);
  return 0;
}
""", "42\n43\n", repeat=10, threads="1,2", comment="Summary of get = {read Formal0.n}: two read-only calls on one object are legal (7.1).")
run("105_disjoint_vectors_methods", """import { Vector3 } from "farmos:math";

function main(): int {
  let u: Vector3 = new Vector3(1.0, 2.0, 3.0);
  let w: Vector3 = new Vector3(1.0, 2.0, 3.0);
  parallel {
    task {
      u.add(new Vector3(1.0, 1.0, 1.0));
    }
    task {
      w.multiplyScalar(2.0);
    }
  }
  println(u);
  println(w);
  return 0;
}
""", "Vector3(2, 3, 4)\nVector3(2, 4, 6)\n", comment="Mutating math methods write only their own receiver (8.2); two different value variables never alias.")

# ---- slow lower-index trap
trap("099_trap_slow_lower_index", """function spin(n: int): int {
  let s: int = 0;
  for (let i = 0; i < n; i += 1) {
    s += i;
  }
  return s;
}

function main(): int {
  let z: int = 0;
  let k: int = 3;
  parallel {
    task {
      println("T1 begin");
      println(spin(300000));
      println(1 / z);
    }
    task {
      println("T2 begin");
      let a: int[1] = [0];
      println(a[k]);
    }
  }
  return 0;
}
""", 101, "division by zero", stdout="T1 begin\n44999850000\n", repeat=10, threads="1,2,4",
     comment="Task 2 traps almost immediately (102) but task 1 (slow) traps later (101): the lowest task index wins, so the runtime MUST let task 1 finish first. Task 1's output up to its trap is flushed; task 2's is dropped (10.2).")


run("107_symbolic_offset_disjoint", """function main(): int {
  let a: int[8] = [0, 0, 0, 0, 0, 0, 0, 0];
  let i: int = 2;
  parallel {
    task {
      a[i] = 1;
    }
    task {
      a[i + 1] = 2;
    }
  }
  println(a[2]);
  println(a[3]);
  return 0;
}
""", "1\n2\n", comment="Same invariant symbol `i` with different constant offsets: provably different elements (5.4). `i` is written by no task of the block.")

run("108_fresh_factory_origins", COUNTER + """
function make(v: int): Counter {
  return new Counter(v);
}

function main(): int {
  const a: Counter = make(1);
  const b: Counter = make(2);
  parallel {
    task {
      a.inc();
    }
    task {
      b.inc();
    }
  }
  println(a.get());
  println(b.get());
  return 0;
}
""", "2\n3\n", repeat=10, threads="1,2",
    comment="`make` is a fresh-return function (5.3): every call site is its own allocation site, so a and b are provably distinct objects.")

print(COUNT, sum(COUNT.values()))
