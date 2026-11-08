// Eclipse - the part of what scripts see that is written in JavaScript itself: the maths helpers, File, Dir and Process
// (over System), Dialog and its controls (over __ui), and include().
#include "EclipseExport.h"

extern char const* const kScriptPrelude[];

char const* const kScriptPrelude[] = {
R"JS(
function Point2d(x, y) { this.x = x || 0; this.y = y || 0; }
Point2d.prototype.add = function (v) { return new Point2d(this.x + v.x, this.y + v.y); };
Point2d.prototype.minus = function (p) { return new Vector2d(this.x - p.x, this.y - p.y); };
Point2d.prototype.distance = function (p) { return Math.sqrt((this.x - p.x) * (this.x - p.x) + (this.y - p.y) * (this.y - p.y)); };
Point2d.prototype.isEqual = function (p) { return this.x === p.x && this.y === p.y; };
Point2d.prototype.midpoint = function (p) { return new Point2d((this.x + p.x) / 2, (this.y + p.y) / 2); };
Point2d.prototype.toString = function () { return "(" + this.x + ", " + this.y + ")"; };

function Point3d(x, y, z) { this.x = x || 0; this.y = y || 0; this.z = z || 0; }
Point3d.prototype.add = function (v) { return new Point3d(this.x + v.x, this.y + v.y, this.z + v.z); };
Point3d.prototype.minus = function (p) { return new Vector3d(this.x - p.x, this.y - p.y, this.z - p.z); };
Point3d.prototype.distance = function (p) { var dx = this.x - p.x, dy = this.y - p.y, dz = this.z - p.z; return Math.sqrt(dx * dx + dy * dy + dz * dz); };
Point3d.prototype.isEqual = function (p) { return this.x === p.x && this.y === p.y && this.z === p.z; };
Point3d.prototype.toString = function () { return "(" + this.x + ", " + this.y + ", " + this.z + ")"; };

function Vector2d(x, y) { this.x = x || 0; this.y = y || 0; }
Vector2d.prototype.add = function (v) { return new Vector2d(this.x + v.x, this.y + v.y); };
Vector2d.prototype.minus = function (v) { return new Vector2d(this.x - v.x, this.y - v.y); };
Vector2d.prototype.multiply = function (k) { return new Vector2d(this.x * k, this.y * k); };
Vector2d.prototype.dot = function (v) { return this.x * v.x + this.y * v.y; };
Vector2d.prototype.length = function () { return Math.sqrt(this.x * this.x + this.y * this.y); };
Vector2d.prototype.normalize = function () { var l = this.length(); if (l > 0) { this.x /= l; this.y /= l; } return this; };
Vector2d.prototype.normalized = function () { var l = this.length() || 1; return new Vector2d(this.x / l, this.y / l); };
Vector2d.prototype.isEqual = function (v) { return this.x === v.x && this.y === v.y; };
Vector2d.prototype.toString = function () { return "(" + this.x + ", " + this.y + ")"; };

function Vector3d(x, y, z) { this.x = x || 0; this.y = y || 0; this.z = z || 0; }
Vector3d.prototype.add = function (v) { return new Vector3d(this.x + v.x, this.y + v.y, this.z + v.z); };
Vector3d.prototype.minus = function (v) { return new Vector3d(this.x - v.x, this.y - v.y, this.z - v.z); };
Vector3d.prototype.multiply = function (k) { return new Vector3d(this.x * k, this.y * k, this.z * k); };
Vector3d.prototype.dot = function (v) { return this.x * v.x + this.y * v.y + this.z * v.z; };
Vector3d.prototype.cross = function (v) { return new Vector3d(this.y * v.z - this.z * v.y, this.z * v.x - this.x * v.z, this.x * v.y - this.y * v.x); };
Vector3d.prototype.length = function () { return Math.sqrt(this.dot(this)); };
Vector3d.prototype.normalize = function () { var l = this.length(); if (l > 0) { this.x /= l; this.y /= l; this.z /= l; } return this; };
Vector3d.prototype.normalized = function () { var l = this.length() || 1; return new Vector3d(this.x / l, this.y / l, this.z / l); };
Vector3d.prototype.isEqual = function (v) { return this.x === v.x && this.y === v.y && this.z === v.z; };
Vector3d.prototype.toString = function () { return "(" + this.x + ", " + this.y + ", " + this.z + ")"; };

function ColorRGBA(r, g, b, a) { this.r = r || 0; this.g = g || 0; this.b = b || 0; this.a = a === undefined ? 255 : a; }
ColorRGBA.prototype.toString = function () {
    var h = function (v) { var s = Math.max(0, Math.min(255, Math.round(v))).toString(16); return s.length < 2 ? "0" + s : s; };
    return "#" + h(this.r) + h(this.g) + h(this.b);
};
ColorRGBA.fromString = function (text) {
    var s = String(text).replace("#", "");
    return new ColorRGBA(parseInt(s.substr(0, 2), 16), parseInt(s.substr(2, 2), 16), parseInt(s.substr(4, 2), 16), s.length >= 8 ? parseInt(s.substr(6, 2), 16) : 255);
};
)JS",
R"JS(
// A 4x4 matrix, row by row (m[row * 4 + column]); points are columns, so a.multiply(b) does b first.
function Matrix4x4(values) { this.m = values ? values.slice(0) : [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]; }
Matrix4x4.prototype.clear = function () { this.m = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]; return this; };
Matrix4x4.prototype.isIdentity = function () { var i = new Matrix4x4(); for (var k = 0; k < 16; ++k) if (Math.abs(this.m[k] - i.m[k]) > 1e-12) return false; return true; };
Matrix4x4.prototype.value = function (row, column) { return this.m[row * 4 + column]; };
Matrix4x4.prototype.setValue = function (row, column, v) { this.m[row * 4 + column] = v; return this; };
Matrix4x4.prototype.multiply = function (b) {
    var r = new Array(16);
    for (var i = 0; i < 4; ++i)
        for (var j = 0; j < 4; ++j) {
            var s = 0;
            for (var k = 0; k < 4; ++k) s += this.m[i * 4 + k] * b.m[k * 4 + j];
            r[i * 4 + j] = s;
        }
    this.m = r;
    return this;
};
Matrix4x4.prototype.translate = function (x, y, z) { return this.multiply(new Matrix4x4([1, 0, 0, x || 0, 0, 1, 0, y || 0, 0, 0, 1, z || 0, 0, 0, 0, 1])); };
Matrix4x4.prototype.scale = function (x, y, z) { y = y === undefined ? x : y; z = z === undefined ? 1 : z; return this.multiply(new Matrix4x4([x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1])); };
Matrix4x4.prototype.rotateRadians = function (angle, axis) {
    var a = axis || new Vector3d(0, 0, 1), v = new Vector3d(a.x, a.y, a.z).normalized(), c = Math.cos(angle), s = Math.sin(angle), t = 1 - c;
    return this.multiply(new Matrix4x4([t * v.x * v.x + c, t * v.x * v.y - s * v.z, t * v.x * v.z + s * v.y, 0,
                                        t * v.x * v.y + s * v.z, t * v.y * v.y + c, t * v.y * v.z - s * v.x, 0,
                                        t * v.x * v.z - s * v.y, t * v.y * v.z + s * v.x, t * v.z * v.z + c, 0, 0, 0, 0, 1]));
};
Matrix4x4.prototype.rotateDegrees = function (angle, axis) { return this.rotateRadians(angle * Math.PI / 180, axis); };
Matrix4x4.prototype.skew = function (degrees) { return this.multiply(new Matrix4x4([1, Math.tan(degrees * Math.PI / 180), 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])); };
Matrix4x4.prototype.transposed = function () { var r = []; for (var i = 0; i < 4; ++i) for (var j = 0; j < 4; ++j) r.push(this.m[j * 4 + i]); return new Matrix4x4(r); };
Matrix4x4.prototype.determinant = function () {
    var m = this.m, d = 0;
    function minor(row, col) {
        var r = [];
        for (var i = 0; i < 4; ++i) if (i !== row) for (var j = 0; j < 4; ++j) if (j !== col) r.push(m[i * 4 + j]);
        return r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) + r[2] * (r[3] * r[7] - r[4] * r[6]);
    }
    for (var c = 0; c < 4; ++c) d += (c % 2 ? -1 : 1) * m[c] * minor(0, c);
    return d;
};
Matrix4x4.prototype.inverse = function () {
    var a = this.m.slice(0), r = new Matrix4x4().m;
    for (var c = 0; c < 4; ++c) {
        var p = c;
        for (var i = c + 1; i < 4; ++i) if (Math.abs(a[i * 4 + c]) > Math.abs(a[p * 4 + c])) p = i;
        if (Math.abs(a[p * 4 + c]) < 1e-15) return null;
        for (var k = 0; k < 4; ++k) { var t = a[c * 4 + k]; a[c * 4 + k] = a[p * 4 + k]; a[p * 4 + k] = t; t = r[c * 4 + k]; r[c * 4 + k] = r[p * 4 + k]; r[p * 4 + k] = t; }
        var d = a[c * 4 + c];
        for (k = 0; k < 4; ++k) { a[c * 4 + k] /= d; r[c * 4 + k] /= d; }
        for (i = 0; i < 4; ++i) {
            if (i === c) continue;
            var f = a[i * 4 + c];
            for (k = 0; k < 4; ++k) { a[i * 4 + k] -= f * a[c * 4 + k]; r[i * 4 + k] -= f * r[c * 4 + k]; }
        }
    }
    return new Matrix4x4(r);
};
Matrix4x4.prototype.multiplyPoint = function (p) {
    var m = this.m, z = p.z || 0;
    var x = m[0] * p.x + m[1] * p.y + m[2] * z + m[3], y = m[4] * p.x + m[5] * p.y + m[6] * z + m[7], w = m[12] * p.x + m[13] * p.y + m[14] * z + m[15];
    var zz = m[8] * p.x + m[9] * p.y + m[10] * z + m[11];
    w = w || 1;
    return p.z === undefined ? new Point2d(x / w, y / w) : new Point3d(x / w, y / w, zz / w);
};
Matrix4x4.prototype.multiplyVector = function (v) {
    var m = this.m, z = v.z || 0;
    var x = m[0] * v.x + m[1] * v.y + m[2] * z, y = m[4] * v.x + m[5] * v.y + m[6] * z, zz = m[8] * v.x + m[9] * v.y + m[10] * z;
    return v.z === undefined ? new Vector2d(x, y) : new Vector3d(x, y, zz);
};
Matrix4x4.prototype.toString = function () { return "[" + this.m.join(", ") + "]"; };
)JS",
R"JS(
function File(path) { this.path = String(path); this._text = ""; this._mode = 0; this._lines = null; this._at = 0; }
File.prototype.exists = function () { return System.exists(this.path); };
File.prototype.open = function (mode) { this._mode = mode === undefined ? 1 : mode; this._text = ""; if (this._mode & 1) { this._lines = null; this._at = 0; } return this._mode !== 1 || this.exists(); };
File.prototype.read = function () { return System.readText(this.path); };
File.prototype.readLine = function () {
    if (!this._lines) this._lines = System.readText(this.path).split("\n");
    return this._at < this._lines.length ? this._lines[this._at++].replace("\r", "") : null;
};
File.prototype.readLines = function () { return System.readText(this.path).replace(/\r/g, "").split("\n"); };
File.prototype.write = function (text) { this._text += String(text); };
File.prototype.writeLine = function (text) { this._text += String(text) + "\n"; };
File.prototype.close = function () { if (this._mode & 2) System.writeText(this.path, this._text, (this._mode & 4) !== 0); this._text = ""; };
File.prototype.remove = function () { return System.removeFile(this.path); };
File.prototype.size = function () { return System.fileSize(this.path); };
// Open modes, as the usual flags: read, write, append.
var FileAccess = { ReadOnly: 1, WriteOnly: 2, ReadWrite: 3, Append: 6 };

function Dir(path) { this.path = path === undefined ? "." : String(path); }
Dir.prototype.exists = function () { return System.isDir(this.path); };
Dir.prototype.mkdir = function (name) { return System.makeDir(name === undefined ? this.path : this.path + "/" + name, false); };
Dir.prototype.mkdirs = function (name) { return System.makeDir(name === undefined ? this.path : this.path + "/" + name, true); };
Dir.prototype.rmdir = function (name) { return System.removeDir(name === undefined ? this.path : this.path + "/" + name); };
Dir.prototype.rename = function (from, to) { return System.renamePath(this.path + "/" + from, this.path + "/" + to); };
Dir.prototype.entryList = function (filter) { return System.entries(this.path, filter || ""); };
Dir.prototype.fileExists = function (name) { return System.exists(this.path + "/" + name); };

function Process(program, args) {
    if (args === undefined && typeof program === "string" && program.indexOf(" ") > 0) { var parts = program.split(" "); program = parts.shift(); args = parts; }
    this.program = program; this.args = args || []; this.exitCode = -1; this.output = ""; this.errorMessage = "";
}
Process.prototype.launchAndRead = function (timeout) {
    var r = System.run(this.program, this.args, timeout || 30000);
    this.exitCode = r.exitCode; this.output = r.output; this.errorMessage = r.error;
    return this.output;
};
Process.prototype.launch = function (timeout) { this.launchAndRead(timeout); return this.exitCode; };
Process.prototype.readStdout = function () { return this.output; };

function include(name) {
    var file = System.scriptFile(String(name));
    if (!file) throw new Error("include: there is no script " + name);
    return (0, eval)(System.readText(file));
}
)JS",
R"JS(
function _Control(kind, label, value) { this.kind = kind; this.label = label || ""; this.value = value; }
function LineEdit() { _Control.call(this, "LineEdit", "", ""); this.text = ""; }
function NumberEdit() { _Control.call(this, "NumberEdit", "", 0); this.decimals = 2; this.minimum = -1e9; this.maximum = 1e9; }
function SpinBox() { _Control.call(this, "SpinBox", "", 0); this.minimum = 0; this.maximum = 99; }
function CheckBox() { _Control.call(this, "CheckBox", "", false); this.checked = false; this.text = ""; }
function ComboBox() { _Control.call(this, "ComboBox", "", ""); this.items = []; this.editable = false; this.currentItem = ""; }
function Label() { _Control.call(this, "Label", "", ""); this.text = ""; }
function GroupBox() { _Control.call(this, "GroupBox", "", ""); this.title = ""; this._controls = []; }
GroupBox.prototype.add = function (control) { this._controls.push(control); };
function _valueOf(c) {
    if (c.kind === "LineEdit") return c.text;
    if (c.kind === "CheckBox") return c.checked;
    if (c.kind === "ComboBox") return c.currentItem;
    return c.value;
}
function _labelOf(c) { return c.kind === "GroupBox" ? c.title : c.kind === "Label" || (c.kind === "CheckBox" && c.text) ? (c.text || c.label) : c.label; }
function _setValue(c, v) {
    if (c.kind === "LineEdit") c.text = v;
    else if (c.kind === "CheckBox") c.checked = !!v;
    else if (c.kind === "ComboBox") c.currentItem = v;
    else c.value = v;
}
function Dialog() { this.title = ""; this.okButtonText = ""; this.cancelButtonText = ""; this._controls = []; }
Dialog.prototype.add = function (control) { this._controls.push(control); };
Dialog.prototype.addSpace = function () { };
Dialog.prototype.newColumn = function () { };
Dialog.prototype.newTab = function (name) { var g = new GroupBox(); g.title = name; this._controls.push(g); };
Dialog.prototype.exec = function () {
    var flat = [];
    function walk(list) {
        for (var i = 0; i < list.length; ++i) {
            flat.push(list[i]);
            if (list[i].kind === "GroupBox") walk(list[i]._controls);
        }
    }
    walk(this._controls);
    var described = { title: this.title, okButtonText: this.okButtonText, cancelButtonText: this.cancelButtonText, controls: [] };
    for (var i = 0; i < flat.length; ++i) {
        var c = flat[i];
        described.controls.push({ kind: c.kind, label: _labelOf(c), value: _valueOf(c), minimum: c.minimum, maximum: c.maximum, decimals: c.decimals,
                                  items: c.items, editable: c.editable });
    }
    var values = __ui.exec(described);
    if (values === null) return false;
    for (i = 0; i < flat.length; ++i) if (flat[i].kind !== "GroupBox" && flat[i].kind !== "Label") _setValue(flat[i], values[i]);
    return true;
};
var Input = {
    getText: function (label, text) { var d = new Dialog(); var e = new LineEdit(); e.label = label; e.text = text || ""; d.add(e); return d.exec() ? e.text : undefined; },
    getNumber: function (label, value, decimals, minimum, maximum) {
        var d = new Dialog(); var e = new NumberEdit(); e.label = label; e.value = value || 0; if (decimals !== undefined) e.decimals = decimals;
        if (minimum !== undefined) e.minimum = minimum; if (maximum !== undefined) e.maximum = maximum; d.add(e); return d.exec() ? e.value : undefined;
    },
    getItem: function (label, items, current) {
        var d = new Dialog(); var e = new ComboBox(); e.label = label; e.items = items; e.currentItem = current === undefined ? items[0] : current; d.add(e);
        return d.exec() ? e.currentItem : undefined;
    }
};
)JS",
nullptr};
