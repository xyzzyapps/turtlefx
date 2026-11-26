// Shared editorial docs enhancements: Modern Logo syntax highlighting,
// copy micro-interaction, and sticky TOC scroll-spy.
(function () {
  const KEYWORDS = new Set([
    "mode", "palette", "fx", "clear", "sky", "light", "camera", "material",
    "to", "end", "on", "frame", "let", "if", "else", "repeat", "with", "while",
    "stop", "return", "and", "or", "not", "branch", "save", "at", "offset",
    "fill", "smooth", "carve", "fd", "forward", "bk", "back", "rt", "right",
    "lt", "left", "pu", "penup", "pd", "pendown", "home", "pitch", "up",
    "down", "yaw", "roll", "color", "pen_width", "pw", "glow", "blend",
    "dot", "circle", "rect", "sphere", "capsule", "box", "cylinder", "cone", "torus"
  ]);

  const BUILTINS = new Set([
    "sin", "cos", "tan", "dsin", "dcos", "rad", "deg", "abs", "sqrt", "pow",
    "floor", "ceil", "fract", "min", "max", "clamp", "lerp", "mix", "rand",
    "hsl", "rgb", "key", "key_pressed", "mouse_x", "mouse_y", "time", "dt",
    "pi", "width", "height", "true", "false", "on", "off"
  ]);

  function esc(s) {
    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  function highlightLogo(code) {
    const tokenRe = /(;[^\n]*|\/\/[^\n]*)|("(?:[^"\\]|\\.)*")|(#[0-9a-fA-F]{3,8}\b)|(\b\d+(?:\.\d+)?%?\b)|(\b[a-zA-Z_][a-zA-Z0-9_]*\b)/g;
    let out = "";
    let last = 0;
    let m;
    while ((m = tokenRe.exec(code)) !== null) {
      if (m.index > last) out += esc(code.slice(last, m.index));
      const [full, comment, str, hex, num, id] = m;
      if (comment) {
        out += `<span class="tok-cm">${esc(comment)}</span>`;
      } else if (str) {
        out += `<span class="tok-str">${esc(str)}</span>`;
      } else if (hex) {
        const swatch = hex.length === 9 ? hex.slice(0, 7) : hex;
        out += `<span class="tok-hex"><span class="hex-dot" style="background:${swatch}"></span>${esc(hex)}</span>`;
      } else if (num) {
        out += `<span class="tok-num">${esc(num)}</span>`;
      } else if (id) {
        const lo = id.toLowerCase();
        if (KEYWORDS.has(lo)) out += `<span class="tok-kw">${esc(id)}</span>`;
        else if (BUILTINS.has(lo)) out += `<span class="tok-fn">${esc(id)}</span>`;
        else out += esc(id);
      }
      last = tokenRe.lastIndex;
    }
    if (last < code.length) out += esc(code.slice(last));
    return out;
  }

  // Highlight Logo code blocks (skip shell/cmake blocks marked data-lang="sh")
  document.querySelectorAll("pre code").forEach((el) => {
    if (el.dataset.lang === "sh") return;
    el.innerHTML = highlightLogo(el.textContent);
  });

  // Subtle inline [Copied] micro-interaction
  document.querySelectorAll(".copy-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      const code = btn.closest(".code-box").querySelector("pre code").textContent;
      navigator.clipboard.writeText(code).then(() => {
        const prev = btn.textContent;
        btn.textContent = "Copied";
        btn.classList.add("copied");
        setTimeout(() => {
          btn.textContent = prev;
          btn.classList.remove("copied");
        }, 1200);
      });
    });
  });

  // Sticky TOC scroll-spy for documentation pages
  const tocLinks = Array.from(document.querySelectorAll(".doc-toc a[href^='#']"));
  if (tocLinks.length > 0 && "IntersectionObserver" in window) {
    const sections = tocLinks
      .map((a) => document.querySelector(a.getAttribute("href")))
      .filter(Boolean);

    const setActive = (id) => {
      tocLinks.forEach((a) => {
        a.classList.toggle("is-active", a.getAttribute("href") === "#" + id);
      });
    };

    const obs = new IntersectionObserver(
      (entries) => {
        const visible = entries.filter((e) => e.isIntersecting);
        if (visible.length > 0) {
          visible.sort((a, b) => a.boundingClientRect.top - b.boundingClientRect.top);
          setActive(visible[0].target.id);
        }
      },
      { rootMargin: "-76px 0px -68% 0px", threshold: 0 }
    );
    sections.forEach((s) => obs.observe(s));
    if (sections[0]) setActive(sections[0].id);
  }
})();
