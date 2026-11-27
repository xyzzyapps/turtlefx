// Live WebAssembly + WebGL 2.0 Studio Controller for TurtleFX GitHub Pages
(function () {
  const EXAMPLES = [
    {
      idx: 0,
      label: "1. Retro Arcade",
      file: "examples/01_retro_arcade.logo",
      hint: "Click canvas to focus · Left/Right (A/D): Move · Space: Fire Lasers · F5: Slow-Mo Debug"
    },
    {
      idx: 1,
      label: "2. Neon 2D",
      file: "examples/02_neon_2d.logo",
      hint: "2D Signed Distance Field strokes + 2-pass Gaussian Bloom · Try Slow-Mo Debug to watch every stroke draw!"
    },
    {
      idx: 2,
      label: "3. 3D Crystal Tree",
      file: "examples/03_3d_crystal_tree.logo",
      hint: "3D SDF Raymarching + Carved Archway + Smooth Clay Island · Try Slow-Mo Debug to watch the 3D tree grow!"
    },
    {
      idx: 3,
      label: "4. GameBoy 3D",
      file: "examples/04_retro_3d.logo",
      hint: "3D SDF Raymarching routed through 320x180 Bayer Dither + GameBoy Palette + Fog"
    },
    {
      idx: 4,
      label: "5. Custom GLSL",
      file: "examples/05_custom_shader.logo",
      hint: "User-authored GLSL shader embedded inline via multiline \"\"\"...\"\"\" string (auto-translated to WebGL 2!)"
    },
    {
      idx: 5,
      label: "6. Actor Swarm",
      file: "examples/06_actor_swarm.logo",
      hint: "Click canvas · WASD/Arrows: Fly · Space: Fire · Turn on Slow-Mo Debug to watch live Actor send/broadcast arrows!"
    },
    {
      idx: 6,
      label: "7. Hades Courtyard",
      file: "examples/07_hades_courtyard.logo",
      hint: "Click canvas · WASD/Arrows: Move · Space: 3-Hit Sword Slash · Shift: Dash · E: Cast · Try Slow-Mo Debug!"
    }
  ];

  const canvas = document.getElementById("canvas");
  const editor = document.getElementById("wasm-editor");
  const statusEl = document.getElementById("wasm-status");
  const hintEl = document.getElementById("wasm-hint");
  const overlayEl = document.getElementById("wasm-loading");
  const debugToggleBtn = document.getElementById("btn-debug-toggle");
  const debugControls = document.getElementById("debug-controls");
  const demoButtons = Array.from(document.querySelectorAll("[data-demo]"));
  const runBtn = document.getElementById("btn-wasm-run");
  const resetBtn = document.getElementById("btn-wasm-reset");

  if (!canvas) return;

  const params = new URLSearchParams(window.location.search);
  const paramDemo = parseInt(params.get("demo") || "7", 10);
  let activeIdx = isNaN(paramDemo) ? 6 : Math.max(0, Math.min(6, paramDemo - 1));
  let debugActive = params.get("debug") === "1" || params.get("debug") === "true";
  let wasmReady = false;

  function setStatus(text, isErr) {
    if (!statusEl) return;
    statusEl.textContent = text;
    statusEl.className = isErr ? "status-err" : "status-ok";
  }

  function updateDebugUI() {
    if (debugToggleBtn) {
      debugToggleBtn.classList.toggle("active", debugActive);
      debugToggleBtn.textContent = debugActive ? "Debug: ON (-debug)" : "Slow-Mo Debug (-debug)";
    }
    if (debugControls) {
      debugControls.classList.toggle("is-active", debugActive);
    }
  }

  async function loadSourceIntoEditor(idx) {
    const ex = EXAMPLES[idx];
    if (!ex) return;
    if (hintEl) hintEl.textContent = ex.hint;
    demoButtons.forEach((b) => {
      b.classList.toggle("active", parseInt(b.dataset.demo, 10) === idx);
    });
    if (!editor) return;
    try {
      const resp = await fetch(ex.file);
      if (resp.ok) {
        editor.value = await resp.text();
      }
    } catch (_) {
      // Ignore fetch errors when opened locally over file://
    }
  }

  function selectExample(idx, forceDebug) {
    activeIdx = idx;
    loadSourceIntoEditor(idx);
    if (wasmReady && window.Module && window.Module._wasm_load_example) {
      window.Module._wasm_load_example(idx);
      if (typeof forceDebug === "boolean") {
        debugActive = forceDebug;
        window.Module._wasm_set_debug(debugActive ? 1 : 0);
        updateDebugUI();
      }
      setStatus("Running " + EXAMPLES[idx].label + " (WebGL 2.0 + WASM)", false);
    }
  }

  function runEditorCode() {
    if (!wasmReady || !window.Module || !editor) return;
    window.Module.ccall("wasm_run_code", null, ["string"], [editor.value]);
    const errPtr = window.Module._wasm_get_error();
    const err = errPtr ? window.Module.UTF8ToString(errPtr) : "";
    if (err && err.length > 0) {
      setStatus("Logo Error: " + err, true);
    } else {
      setStatus("Running live editor code (WebGL 2.0 + WASM)", false);
    }
  }

  // Wire example buttons
  demoButtons.forEach((btn) => {
    btn.addEventListener("click", () => {
      selectExample(parseInt(btn.dataset.demo, 10));
      canvas.focus();
    });
  });

  // Wire Debug toggle & transport buttons
  if (debugToggleBtn) {
    debugToggleBtn.addEventListener("click", () => {
      debugActive = !debugActive;
      if (wasmReady && window.Module._wasm_set_debug) {
        window.Module._wasm_set_debug(debugActive ? 1 : 0);
      }
      updateDebugUI();
      canvas.focus();
    });
  }

  document.querySelectorAll("[data-dbg-cmd]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const cmd = parseInt(btn.dataset.dbgCmd, 10);
      if (!debugActive) {
        debugActive = true;
        updateDebugUI();
      }
      if (wasmReady && window.Module._wasm_debug_cmd) {
        window.Module._wasm_debug_cmd(cmd);
      }
      canvas.focus();
    });
  });

  // Quick-launch Debug showcase buttons
  document.querySelectorAll("[data-launch-debug]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const idx = parseInt(btn.dataset.launchDebug, 10);
      selectExample(idx, true);
      canvas.scrollIntoView({ behavior: "smooth", block: "center" });
      canvas.focus();
    });
  });

  if (runBtn) runBtn.addEventListener("click", runEditorCode);
  if (resetBtn) resetBtn.addEventListener("click", () => selectExample(activeIdx));

  // Gate keyboard input cleanly between the WASM game canvas and the Logo <textarea>
  if (editor) {
    editor.addEventListener("focus", () => {
      if (wasmReady && window.Module._wasm_set_input_enabled) {
        window.Module._wasm_set_input_enabled(0);
      }
    });
    editor.addEventListener("blur", () => {
      if (wasmReady && window.Module._wasm_set_input_enabled) {
        window.Module._wasm_set_input_enabled(1);
      }
    });
    editor.addEventListener("keydown", (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
        e.preventDefault();
        runEditorCode();
      }
    });
  }

  canvas.addEventListener("mousedown", () => {
    canvas.focus();
    if (wasmReady && window.Module._wasm_set_input_enabled) {
      window.Module._wasm_set_input_enabled(1);
    }
  });

  // Prevent arrow keys / Space from scrolling page when canvas is focused
  window.addEventListener("keydown", (e) => {
    if (document.activeElement === canvas) {
      if (["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Space", "Tab"].includes(e.code)) {
        e.preventDefault();
      }
      if (e.code === "F5") {
        e.preventDefault();
        setTimeout(() => {
          if (wasmReady && window.Module._wasm_get_debug_enabled) {
            debugActive = window.Module._wasm_get_debug_enabled() !== 0;
            updateDebugUI();
          }
        }, 30);
      }
    }
  });

  loadSourceIntoEditor(activeIdx);
  updateDebugUI();

  // Configure Emscripten Module and dynamically load wasm/turtlefx.js
  window.Module = {
    canvas: canvas,
    locateFile: function (path) {
      return "wasm/" + path;
    },
    onRuntimeInitialized: function () {
      wasmReady = true;
      if (overlayEl) overlayEl.style.display = "none";
      if (window.Module._wasm_load_example) {
        window.Module._wasm_load_example(activeIdx);
      }
      if (debugActive && window.Module._wasm_set_debug) {
        window.Module._wasm_set_debug(1);
      }
      setStatus("Running " + EXAMPLES[activeIdx].label + " (WebGL 2.0 + WASM)", false);
    },
    printErr: function (text) {
      console.warn("[TurtleFX WASM]", text);
    }
  };

  const script = document.createElement("script");
  script.src = "wasm/turtlefx.js";
  script.async = true;
  script.onerror = function () {
    if (overlayEl) {
      overlayEl.innerHTML =
        '<div class="wasm-fallback"><strong>WASM build not found locally.</strong><br>' +
        'Visit the live GitHub Pages site at <a href="https://xyzzyapps.github.io/turtlefx/">xyzzyapps.github.io/turtlefx</a> ' +
        "or build with Emscripten (<code>emcmake cmake -B build-wasm</code>).</div>";
    }
    setStatus("Waiting for WASM bundle (wasm/turtlefx.js)", true);
  };
  document.body.appendChild(script);
})();
