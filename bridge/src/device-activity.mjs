// When the Cardputer last reached the bridge, and how often, for the web page's Status screen.
// In memory: after a restart the strip starts empty and fills as the device checks in.
const MINUTE = 60_000;
const MINUTES = 60;
const MAX_STARTS = 40;

export class DeviceActivity {
  constructor({ now = () => Date.now() } = {}) {
    this.now = now;
    this.lastSeen = 0;
    this.lastPath = "";
    this.buckets = new Map(); // minute number -> requests
    this.starts = [];          // { at, reset, note }
  }

  seen(pathname = "") {
    const now = this.now();
    this.lastSeen = now;
    this.lastPath = String(pathname).slice(0, 80);
    const minute = Math.floor(now / MINUTE);
    this.buckets.set(minute, (this.buckets.get(minute) || 0) + 1);
    for (const key of this.buckets.keys()) if (key <= minute - MINUTES) this.buckets.delete(key);
  }

  // From the line bootReportLine makes: "[device] started device=... reset=power-on note=..."
  started(line) {
    const reset = /\breset=([a-z0-9-]+)/u.exec(line)?.[1] || "unknown";
    const note = /\bnote=(\S+)/u.exec(line)?.[1] || "";
    this.starts.push({ at: this.now(), reset, note });
    if (this.starts.length > MAX_STARTS) this.starts.shift();
  }

  view() {
    const now = this.now();
    const minute = Math.floor(now / MINUTE);
    const strip = [];
    for (let m = minute - MINUTES + 1; m <= minute; m += 1) strip.push(this.buckets.get(m) || 0);
    return {
      last_seen_ms: this.lastSeen ? now - this.lastSeen : null,
      last_seen_at: this.lastSeen || null,
      strip,
      strip_start: (minute - MINUTES + 1) * MINUTE,
      starts: [...this.starts].reverse()
    };
  }
}

// The note the firmware writes before it restarts itself: "<why>_f<free KB>_b<largest KB>_<screen>",
// like "memory_f13_b7_Pulse". Anything else as it came.
export function describeStart({ reset, note }) {
  const noted = /^([a-z]+)_f(\d+)_b(\d+)_?(.*)$/u.exec(note || "");
  const why = {
    "power-on": "Turned on", "software": "Restarted itself", "crash": "Crashed and restarted",
    "task-watchdog": "Froze and restarted", "interrupt-watchdog": "Froze and restarted", "watchdog": "Froze and restarted",
    "brownout": "Lost power", "deep-sleep": "Woke up", "reset-pin": "Reset button"
  }[reset] || "Started";
  if (noted) {
    const where = noted[4] ? ` in ${noted[4].replace(/_/gu, " ")}` : "";
    const what = noted[1] === "memory" ? "ran low on memory" : noted[1];
    return { title: why, detail: `${what}${where} (${noted[2]} KB free)`, warn: true };
  }
  const bad = ["crash", "task-watchdog", "interrupt-watchdog", "watchdog", "brownout"].includes(reset);
  return { title: why, detail: note ? note.replace(/_/gu, " ") : reset.replace(/-/gu, " "), warn: bad };
}
