// Minimal reader for binary FBX 7.x, enough to pull geometry out of a Blender
// export: node names, vertex arrays and polygon index arrays. No materials, no
// animation, no transforms beyond the model translation -- this exists to
// measure bones, not to render a scene.
import { readFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';

const NULL_RECORD_SMALL = 13;
const NULL_RECORD_LARGE = 25;

class Reader {
  constructor(buffer) {
    this.buffer = buffer;
    this.view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);
    this.offset = 0;
  }
  u8() { const v = this.view.getUint8(this.offset); this.offset += 1; return v; }
  u32() { const v = this.view.getUint32(this.offset, true); this.offset += 4; return v; }
  u64() { const v = Number(this.view.getBigUint64(this.offset, true)); this.offset += 8; return v; }
  i8() { const v = this.view.getInt8(this.offset); this.offset += 1; return v; }
  i32() { const v = this.view.getInt32(this.offset, true); this.offset += 4; return v; }
  i64() { const v = Number(this.view.getBigInt64(this.offset, true)); this.offset += 8; return v; }
  f32() { const v = this.view.getFloat32(this.offset, true); this.offset += 4; return v; }
  f64() { const v = this.view.getFloat64(this.offset, true); this.offset += 8; return v; }
  bytes(n) {
    const out = this.buffer.subarray(this.offset, this.offset + n);
    this.offset += n;
    return out;
  }
}

function readProperty(reader) {
  const type = String.fromCharCode(reader.u8());
  switch (type) {
    case 'Y': return reader.view.getInt16(reader.offset, true), (reader.offset += 2, 0);
    case 'C': return reader.u8() !== 0;
    case 'I': return reader.i32();
    case 'F': return reader.f32();
    case 'D': return reader.f64();
    case 'L': return reader.i64();
    case 'S': { const size = reader.u32(); return reader.bytes(size).toString('latin1'); }
    case 'R': { const size = reader.u32(); return reader.bytes(size); }
    case 'f': case 'd': case 'l': case 'i': case 'b': {
      const count = reader.u32();
      const encoding = reader.u32();
      const compressed = reader.u32();
      if (encoding === 1) {
        // Arrays are deflated on disk. Inflate into the right typed array, or the
        // caller sees bytes and every coordinate reads as NaN.
        const raw = inflateSync(reader.bytes(compressed));
        if (type === 'd' || type === 'l') return new Float64Array(raw.buffer, raw.byteOffset, count);
        if (type === 'f') return new Float32Array(raw.buffer, raw.byteOffset, count);
        if (type === 'i') return new Int32Array(raw.buffer, raw.byteOffset, count);
        return raw;
      }
      if (type === 'd') {
        const out = new Float64Array(count);
        for (let i = 0; i < count; ++i) out[i] = reader.f64();
        return out;
      }
      if (type === 'f') {
        const out = new Float32Array(count);
        for (let i = 0; i < count; ++i) out[i] = reader.f32();
        return out;
      }
      if (type === 'i') {
        const out = new Int32Array(count);
        for (let i = 0; i < count; ++i) out[i] = reader.i32();
        return out;
      }
      if (type === 'l') {
        const out = new Float64Array(count);
        for (let i = 0; i < count; ++i) out[i] = reader.i64();
        return out;
      }
      return reader.bytes(count);
    }
    default:
      throw new Error(`Unknown FBX property type ${type} at ${reader.offset}`);
  }
}

function readNode(reader, wide, largeNull) {
  const start = reader.offset;
  const end = wide ? reader.u64() : reader.u32();
  const propertyCount = wide ? reader.u64() : reader.u32();
  const propertyBytes = wide ? reader.u64() : reader.u32();
  const nameLength = reader.u8();
  if (end === 0 && propertyCount === 0 && propertyBytes === 0 && nameLength === 0) return null;
  const name = reader.bytes(nameLength).toString('latin1');
  const properties = [];
  for (let i = 0; i < propertyCount; ++i) properties.push(readProperty(reader));
  const children = [];
  while (reader.offset < end - (largeNull ? NULL_RECORD_LARGE : NULL_RECORD_SMALL)) {
    const child = readNode(reader, wide, largeNull);
    if (!child) break;
    children.push(child);
  }
  reader.offset = end;
  return { name, properties, children, start };
}

export function readFbx(path) {
  const buffer = readFileSync(path);
  const reader = new Reader(buffer);
  const magic = buffer.subarray(0, 21).toString('latin1');
  if (!magic.startsWith('Kaydara FBX Binary')) throw new Error('Not a binary FBX file');
  reader.offset = 23;
  const version = reader.u32();
  const wide = version >= 7500;
  const largeNull = version >= 7500;
  const roots = [];
  while (reader.offset < buffer.length) {
    const node = readNode(reader, wide, largeNull);
    if (!node) break;
    roots.push(node);
  }
  return { version, roots };
}

export function findNodes(nodes, name, out = []) {
  for (const node of nodes) {
    if (node.name === name) out.push(node);
    if (node.children.length) findNodes(node.children, name, out);
  }
  return out;
}

// Geometry records arrive as `Geometry` nodes; the human-readable name lives on
// the `Model` node that the geometry is connected to. `Connections` lists those
// as C: "OO", childId, parentId -- so a geometry id can be resolved to a name.
// Blender writes object names as "Namespace::Name" and separates the name from
// its FBX class with a 0x00 0x01 pair, so both have to come off before the name
// is usable.
function cleanName(raw) {
  let name = String(raw);
  const separator = name.indexOf('\u0000');
  if (separator >= 0) name = name.slice(0, separator);
  name = name.replace(/^[A-Za-z]+::/, '');
  return name.trim();
}

export function geometryByName(roots) {
  const models = new Map();
  const connections = findNodes(roots, 'Connections');
  const idToName = new Map();
  for (const model of findNodes(roots, 'Model')) {
    const id = model.properties[0];
    const name = cleanName(model.properties[1]);
    idToName.set(id, name);
  }
  const parents = [];
  for (const group of connections) {
    for (const entry of group.children) {
      if (entry.name !== 'C' || entry.properties.length < 3) continue;
      const [, child, parent] = entry.properties;
      parents.push([child, parent]);
    }
  }
  const parentOf = new Map(parents.map(([child, parent]) => [child, parent]));
  const result = new Map();
  for (const geometry of findNodes(roots, 'Geometry')) {
    const id = geometry.properties[0];
    const ownName = cleanName(geometry.properties[1]);
    let name = ownName;
    let cursor = id;
    for (let hop = 0; hop < 4 && cursor !== undefined; ++hop) {
      const parent = parentOf.get(cursor);
      if (parent === undefined) break;
      if (idToName.has(parent)) { name = idToName.get(parent); break; }
      cursor = parent;
    }
    const vertices = geometry.children.find(child => child.name === 'Vertices');
    const indices = geometry.children.find(child => child.name === 'PolygonVertexIndex');
    if (!vertices || !indices) continue;
    result.set(name, { vertices: vertices.properties[0], indices: indices.properties[0] });
  }
  return result;
}
