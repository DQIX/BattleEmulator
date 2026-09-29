// Read-only projection of existing ROM-mined camera metadata for Slime.dst.
import {readFile} from 'node:fs/promises';
import path from 'node:path';
const root = import.meta.dirname;
const b = await readFile(path.join(root, 'camera/freecam-membership-metadata.bin'));
if (b.toString('ascii', 0, 4) !== 'FCMM' || b.readUInt32LE(4) !== 4) throw Error('unexpected membership format');
const actions = b.readUInt32LE(8), profiles = b.readUInt32LE(12);
const players = b.readUInt32LE(16), monsters = b.readUInt32LE(20), specials = b.readUInt32LE(24);
let offset = 32 + (profiles + 1) * actions * 12;
const playerRows = Array.from({length: players}, (_, i) => ({body:b.readUInt16LE(offset+i*8), weapon:b.readUInt16LE(offset+i*8+2), profile:b.readUInt32LE(offset+i*8+4)}));
offset += players * 8;
const monsterRows = Array.from({length:monsters}, (_, i)=>({id:b.readUInt16LE(offset+i*8),profile:b.readUInt32LE(offset+i*8+4)})).filter(r=>[1,0x39,0x122,0x124].includes(r.id));
offset += monsters * 8;
const specialRows = Array.from({length:specials}, (_, i)=>({id:b.readUInt16LE(offset+i*8),profile:b.readUInt32LE(offset+i*8+4)}));
const cell = (profile, action) => {
  const p = 32 + (profile * actions + action) * 12;
  return {projection:b.readUInt32LE(p),count:b.readUInt16LE(p+4),tracking:b.readUInt16LE(p+6),opcode4f:b[p+8]};
};
console.log(JSON.stringify({playerRows,monsterRows,specialRows,cells:[...playerRows.filter(r=>r.body===2&&r.weapon===1),...monsterRows,...specialRows].map(r=>({...r,attack:cell(r.profile,1),heal:cell(r.profile,30)}))},null,2));
