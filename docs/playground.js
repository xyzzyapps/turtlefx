// Lightweight Browser Mini-Interpreter for Modern Logo (2D & Retro Sandbox)
(function () {
  const PRESETS = {
    star: `; Try changing 5 -> 7 or 144 -> 154!
mode 2d
fx bloom(strength: 0.7)

on frame(dt)
  clear #06070d
  color hsl((time * 50) % 360, 100%, 62%)
  pen_width 3
  glow 0.8

  rt time * 25
  pu  bk 35  rt 18  pd
  repeat 5 [
    fd 90
    rt 144
  ]
end`,

    spiral: `; Rainbow turtle spiral!
mode 2d
fx bloom(strength: 0.6)

on frame(dt)
  clear #06070d
  pen_width 2.2
  glow 0.6
  rt time * 20

  repeat 48 with i [
    color hsl(i * 8 + time * 90, 100%, 60%)
    fd i * 2.8
    rt 58 + sin(time) * 4
  ]
end`,

    ship: `; Retro PICO-8 style rocket!
mode retro(260, 190)

on frame(dt)
  clear #1d2b53

  ; Stars
  repeat 25 with i [
    let sx = ((i * 73) % 260) - 130
    let sy = 95 - ((i * 41 + time * 35) % 190)
    color #c2c3c7
    at sx sy [ dot 1.2 ]
  ]

  ; Bobbing rocket
  at sin(time * 2.2) * 55 -20 [
    color #fff1e8
    pen_width 3
    repeat 3 [ fd 26  rt 120 ]
    color #29adff
    dot 4
    color #ffa300
    rt 180
    fd 10 + sin(time * 30) * 5
  ]
end`,

    mandala: `; Spinning neon mandala using branch [ ... ]
mode 2d
fx bloom(strength: 0.65)

on frame(dt)
  clear #06070d
  rt time * 22

  repeat 10 with i [
    branch [
      rt i * 36
      pu  fd 65 + sin(time * 3 + i) * 18  pd
      color hsl(i * 36 + time * 50, 100%, 60%)
      pen_width 2.2
      glow 0.7
      repeat 6 [
        fd 22
        rt 60
      ]
    ]
  ]
end`
  };

  const editor = document.getElementById("code-editor");
  const canvas = document.getElementById("pg-canvas");
  const statusEl = document.getElementById("pg-status");
  const ctx = canvas.getContext("2d");

  let activePreset = "star";
  let parsedProgram = null;
  let startTime = performance.now();

  // --- Copy buttons ---
  document.querySelectorAll(".copy-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      const code = btn.closest(".code-box").querySelector("pre code").textContent;
      navigator.clipboard.writeText(code).then(() => {
        const prev = btn.textContent;
        btn.textContent = "Copied!";
        setTimeout(() => (btn.textContent = prev), 1200);
      });
    });
  });

  // --- Tokenizer & Parser ---
  function tokenize(src) {
    const tokens = [];
    let i = 0;
    while (i < src.length) {
      const c = src[i];
      if (c === " " || c === "\t" || c === "\r") { i++; continue; }
      if (c === ";" || (c === "/" && src[i + 1] === "/")) {
        while (i < src.length && src[i] !== "\n") i++;
        continue;
      }
      if (c === "\n") { tokens.push({ t: "NL" }); i++; continue; }
      if (c === "#") {
        let j = i + 1;
        while (j < src.length && /[0-9a-fA-F]/.test(src[j])) j++;
        tokens.push({ t: "COLOR", v: src.slice(i, j) });
        i = j;
        continue;
      }
      if (/[0-9.]/.test(c)) {
        const m = src.slice(i).match(/^\d+(\.\d+)?/);
        tokens.push({ t: "NUM", v: parseFloat(m[0]) });
        i += m[0].length;
        if (src[i] === "%") i++;
        continue;
      }
      if (/[a-zA-Z_]/.test(c)) {
        const m = src.slice(i).match(/^[a-zA-Z_][a-zA-Z0-9_]*/);
        tokens.push({ t: "ID", v: m[0].toLowerCase() });
        i += m[0].length;
        continue;
      }
      if ("()[]+-*/%:,=<>".includes(c)) {
        tokens.push({ t: c });
        i++;
        continue;
      }
      i++;
    }
    return tokens;
  }

  function parseProgram(src) {
    const toks = tokenize(src);
    let p = 0;
    const cur = () => toks[p] || { t: "EOF" };
    const skipNL = () => { while (cur().t === "NL") p++; };

    function parseExpr() { return parseAdd(); }
    function parseAdd() {
      let left = parseMul();
      while (cur().t === "+" || cur().t === "-") {
        const op = cur().t; p++;
        left = { k: "bin", op, l: left, r: parseMul() };
      }
      return left;
    }
    function parseMul() {
      let left = parsePrim();
      while (cur().t === "*" || cur().t === "/" || cur().t === "%") {
        const op = cur().t; p++;
        left = { k: "bin", op, l: left, r: parsePrim() };
      }
      return left;
    }
    function parsePrim() {
      const t = cur();
      if (t.t === "-") { p++; return { k: "neg", v: parsePrim() }; }
      if (t.t === "NUM") { p++; return { k: "num", v: t.v }; }
      if (t.t === "COLOR") { p++; return { k: "str", v: t.v }; }
      if (t.t === "(") { p++; const e = parseExpr(); if (cur().t === ")") p++; return e; }
      if (t.t === "ID") {
        p++;
        if (cur().t === "(") {
          p++;
          const args = [];
          while (cur().t !== ")" && cur().t !== "EOF") {
            if (cur().t === "ID" && toks[p + 1] && toks[p + 1].t === ":") p += 2;
            args.push(parseExpr());
            if (cur().t === ",") p++; else break;
          }
          if (cur().t === ")") p++;
          return { k: "call", name: t.v, args };
        }
        return { k: "id", name: t.v };
      }
      p++;
      return { k: "num", v: 0 };
    }

    function parseBlock() {
      skipNL();
      if (cur().t !== "[") throw new Error("Expected '['");
      p++;
      const body = [];
      while (cur().t !== "]" && cur().t !== "EOF") {
        skipNL();
        if (cur().t === "]") break;
        body.push(parseStmt());
      }
      if (cur().t === "]") p++;
      return body;
    }

    function parseStmt() {
      const t = cur();
      if (t.t === "ID" && t.v === "let") {
        p++;
        const name = cur().v; p++;
        if (cur().t === "=") p++;
        return { k: "let", name, val: parseExpr() };
      }
      if (t.t === "ID" && t.v === "repeat") {
        p++;
        const count = parseExpr();
        let iter = null;
        if (cur().t === "ID" && cur().v === "with") { p++; iter = cur().v; p++; }
        return { k: "repeat", count, iter, body: parseBlock() };
      }
      if (t.t === "ID" && (t.v === "branch" || t.v === "at")) {
        const name = t.v; p++;
        const args = [];
        while (cur().t !== "[" && cur().t !== "NL" && cur().t !== "EOF") args.push(parseExpr());
        return { k: name, args, body: parseBlock() };
      }
      if (t.t === "ID" && t.v === "on") {
        p += 2; // on frame
        if (cur().t === "(") { while (cur().t !== ")" && cur().t !== "EOF") p++; if (cur().t === ")") p++; }
        const body = [];
        while (cur().t !== "EOF") {
          skipNL();
          if (cur().t === "ID" && cur().v === "end") { p++; break; }
          body.push(parseStmt());
        }
        return { k: "on_frame", body };
      }
      if (t.t === "ID") {
        const name = t.v; p++;
        const zeroArg = ["pu", "penup", "pd", "pendown", "home"].includes(name);
        const args = [];
        if (cur().t === "(") {
          p++;
          while (cur().t !== ")" && cur().t !== "EOF") {
            if (cur().t === "ID" && toks[p + 1] && toks[p + 1].t === ":") p += 2;
            args.push(parseExpr());
            if (cur().t === ",") p++; else break;
          }
          if (cur().t === ")") p++;
        } else if (!zeroArg && cur().t !== "NL" && cur().t !== "]" && cur().t !== "EOF") {
          args.push(parseExpr());
        }
        return { k: "cmd", name, args };
      }
      p++;
      return { k: "noop" };
    }

    const stmts = [];
    while (cur().t !== "EOF") {
      skipNL();
      if (cur().t === "EOF") break;
      stmts.push(parseStmt());
    }
    return stmts;
  }

  function runFrame(stmts, timeSec) {
    const vars = { time: timeSec, pi: Math.PI };
    const state = {
      x: 0, y: 0, angle: 0, pen: true,
      color: "#ffffff", width: 2, glow: 0,
      clear: "#06070d", bloom: 0, retro: false
    };
    const stack = [];
    const ops = [];

    function evalE(e) {
      if (!e) return 0;
      if (e.k === "num") return e.v;
      if (e.k === "str") return e.v;
      if (e.k === "neg") return -evalE(e.v);
      if (e.k === "id") return vars[e.name] !== undefined ? vars[e.name] : e.name;
      if (e.k === "bin") {
        const l = Number(evalE(e.l)), r = Number(evalE(e.r));
        if (e.op === "+") return l + r;
        if (e.op === "-") return l - r;
        if (e.op === "*") return l * r;
        if (e.op === "/") return r ? l / r : 0;
        if (e.op === "%") return r ? ((l % r) + r) % r : 0;
      }
      if (e.k === "call") {
        const a = e.args.map(evalE);
        if (e.name === "sin") return Math.sin(a[0] || 0);
        if (e.name === "cos") return Math.cos(a[0] || 0);
        if (e.name === "abs") return Math.abs(a[0] || 0);
        if (e.name === "hsl") return `hsl(${a[0] || 0}, ${a[1] || 100}%, ${a[2] || 50}%)`;
        if (e.name === "retro") { state.retro = true; return "retro"; }
      }
      return 0;
    }

    function execList(list) {
      for (const s of list) {
        if (s.k === "let") vars[s.name] = evalE(s.val);
        else if (s.k === "repeat") {
          const n = Math.min(400, Math.max(0, Math.floor(Number(evalE(s.count)))));
          for (let i = 0; i < n; i++) {
            if (s.iter) vars[s.iter] = i;
            execList(s.body);
          }
        } else if (s.k === "branch") {
          stack.push({ ...state });
          execList(s.body);
          Object.assign(state, stack.pop());
        } else if (s.k === "at") {
          stack.push({ ...state });
          state.x = Number(evalE(s.args[0]) || 0);
          state.y = Number(evalE(s.args[1]) || 0);
          state.angle = 0;
          execList(s.body);
          Object.assign(state, stack.pop());
        } else if (s.k === "cmd") {
          const v0 = s.args[0] ? evalE(s.args[0]) : 0;
          if (s.name === "mode" && v0 === "retro") state.retro = true;
          else if (s.name === "clear") state.clear = String(v0);
          else if (s.name === "color") state.color = String(v0);
          else if (s.name === "pen_width" || s.name === "pw") state.width = Number(v0);
          else if (s.name === "glow") state.glow = Number(v0);
          else if (s.name === "fx") state.bloom = 1;
          else if (s.name === "pu" || s.name === "penup") state.pen = false;
          else if (s.name === "pd" || s.name === "pendown") state.pen = true;
          else if (s.name === "rt" || s.name === "right") state.angle += Number(v0);
          else if (s.name === "lt" || s.name === "left") state.angle -= Number(v0);
          else if (s.name === "dot" || s.name === "circle") {
            ops.push({ k: "dot", x: state.x, y: state.y, r: Number(v0), c: state.color, g: state.glow });
          } else if (s.name === "fd" || s.name === "bk") {
            const d = s.name === "bk" ? -Number(v0) : Number(v0);
            const rad = (state.angle * Math.PI) / 180;
            const nx = state.x + Math.sin(rad) * d;
            const ny = state.y + Math.cos(rad) * d;
            if (state.pen) {
              ops.push({ k: "line", x0: state.x, y0: state.y, x1: nx, y1: ny, c: state.color, w: state.width, g: state.glow });
            }
            state.x = nx; state.y = ny;
          }
        }
      }
    }

    const top = stmts.filter((s) => s.k !== "on_frame");
    const frame = stmts.find((s) => s.k === "on_frame");
    execList(top);
    if (frame) execList(frame.body);

    // Render to Canvas
    const W = canvas.width, H = canvas.height;
    ctx.fillStyle = state.clear;
    ctx.fillRect(0, 0, W, H);

    ctx.save();
    ctx.translate(W / 2, H / 2);
    const scale = state.retro ? 1.8 : 1.35;

    for (const op of ops) {
      ctx.shadowColor = op.g > 0 ? op.c : "transparent";
      ctx.shadowBlur = op.g * (state.bloom ? 18 : 8);
      if (op.k === "line") {
        ctx.strokeStyle = op.c;
        ctx.lineWidth = op.w * scale;
        ctx.lineCap = state.retro ? "butt" : "round";
        ctx.beginPath();
        ctx.moveTo(op.x0 * scale, -op.y0 * scale);
        ctx.lineTo(op.x1 * scale, -op.y1 * scale);
        ctx.stroke();
      } else if (op.k === "dot") {
        ctx.fillStyle = op.c;
        ctx.beginPath();
        ctx.arc(op.x * scale, -op.y * scale, Math.max(1, op.r * scale), 0, Math.PI * 2);
        ctx.fill();
      }
    }
    ctx.restore();

    if (state.retro) {
      ctx.fillStyle = "rgba(0,0,0,0.22)";
      for (let y = 0; y < H; y += 3) ctx.fillRect(0, y, W, 1);
    }
  }

  function compileEditor() {
    try {
      parsedProgram = parseProgram(editor.value);
      statusEl.textContent = "Running at 60 FPS";
      statusEl.className = "status-ok";
    } catch (err) {
      statusEl.textContent = "Syntax hint: " + err.message;
      statusEl.className = "status-err";
    }
  }

  function loadPreset(name) {
    activePreset = name;
    editor.value = PRESETS[name];
    document.querySelectorAll(".preset-btn").forEach((b) => {
      b.classList.toggle("active", b.dataset.preset === name);
    });
    compileEditor();
  }

  document.querySelectorAll(".preset-btn").forEach((b) => {
    b.addEventListener("click", () => loadPreset(b.dataset.preset));
  });
  document.getElementById("reset-btn").addEventListener("click", () => loadPreset(activePreset));
  editor.addEventListener("input", compileEditor);

  function tick(now) {
    if (parsedProgram) {
      try {
        runFrame(parsedProgram, (now - startTime) * 0.001);
      } catch (_) {}
    }
    requestAnimationFrame(tick);
  }

  loadPreset("star");
  requestAnimationFrame(tick);
})();
