// three.js viewport for the Geometry view: STL triangles + temperature point
// cloud, orbit/pan/zoom, fit-to-view and node picking. Renders on demand
// (only when something changes) so an idle tab costs no CPU.

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/OrbitControls.js';

// Single-hue warm ramps for temperature (low -> high). Light theme goes light
// to dark; dark theme goes dark to light so hot nodes stand out on a dark
// background. Never a rainbow.
const RAMPS = {
  light: ['#f9c9ab', '#f29a6a', '#eb6834', '#b8461a', '#6e2408'],
  dark: ['#6e2a10', '#a8441b', '#e0632f', '#f3a074', '#fbd9c3'],
};

export function rampFor(theme) {
  return RAMPS[theme === 'dark' ? 'dark' : 'light'];
}

function sampleRamp(stops, t) {
  const f = Math.min(Math.max(t, 0), 1) * (stops.length - 1);
  const i = Math.min(Math.floor(f), stops.length - 2);
  return new THREE.Color(stops[i]).lerp(new THREE.Color(stops[i + 1]), f - i);
}

export class Viewer3D {
  constructor(container, { onHover, onPick } = {}) {
    this.container = container;
    this.onHover = onHover;
    this.onPick = onPick;

    this.renderer = new THREE.WebGLRenderer({ antialias: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    container.prepend(this.renderer.domElement);

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(45, 1, 0.0001, 1000);
    this.camera.position.set(0.1, -0.12, 0.08);
    this.camera.up.set(0, 0, 1);  // CAD convention: z up

    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = false;
    this.controls.screenSpacePanning = true;
    this.controls.addEventListener('change', () => this.requestRender());

    this.scene.add(new THREE.HemisphereLight(0xffffff, 0x666666, 1.6));
    const sun = new THREE.DirectionalLight(0xffffff, 1.4);
    sun.position.set(1, -2, 3);
    this.scene.add(sun);

    this.meshGroup = new THREE.Group();
    this.pointsGroup = new THREE.Group();
    this.scene.add(this.meshGroup, this.pointsGroup);

    this.marker = new THREE.Mesh(new THREE.SphereGeometry(1, 16, 12), new THREE.MeshBasicMaterial({ color: 0x2a78d6 }));
    this.marker.visible = false;
    this.scene.add(this.marker);

    this.raycaster = new THREE.Raycaster();
    this.pointer = new THREE.Vector2();
    this.pointSize = 4;
    this.points = null;
    this.nodeData = null;

    const canvas = this.renderer.domElement;
    canvas.addEventListener('pointermove', (e) => this.handlePointer(e, false));
    canvas.addEventListener('click', (e) => this.handlePointer(e, true));
    canvas.addEventListener('pointerleave', () => this.onHover?.(null));

    new ResizeObserver(() => this.resize()).observe(container);
    this.resize();
  }

  setBackground(cssColor) {
    this.scene.background = new THREE.Color(cssColor);
    this.requestRender();
  }

  resize() {
    const w = this.container.clientWidth, h = this.container.clientHeight;
    if (!w || !h) return;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
    this.requestRender();
  }

  requestRender() {
    if (this.pending) return;
    this.pending = true;
    requestAnimationFrame(() => {
      this.pending = false;
      this.renderer.render(this.scene, this.camera);
    });
  }

  clearGroup(group) {
    for (const child of [...group.children]) {
      group.remove(child);
      child.geometry?.dispose();
      child.material?.dispose();
    }
  }

  // positions: flat [x,y,z, ...] in file units; scale converts them to metres.
  setMesh(positions, scale) {
    this.clearGroup(this.meshGroup);
    const geom = new THREE.BufferGeometry();
    const arr = new Float32Array(positions.length);
    for (let i = 0; i < positions.length; i++) arr[i] = positions[i] * scale;
    geom.setAttribute('position', new THREE.BufferAttribute(arr, 3));
    geom.computeVertexNormals();
    geom.computeBoundingSphere();

    const solid = new THREE.Mesh(geom, new THREE.MeshStandardMaterial({
      color: 0xb9c3cf, metalness: 0.1, roughness: 0.75, side: THREE.DoubleSide,
      transparent: true, opacity: 0.55, depthWrite: false,
      polygonOffset: true, polygonOffsetFactor: 1, polygonOffsetUnits: 1,
    }));
    const edges = new THREE.LineSegments(new THREE.EdgesGeometry(geom, 25), new THREE.LineBasicMaterial({ color: 0x55606e }));
    const wire = new THREE.LineSegments(new THREE.WireframeGeometry(geom), new THREE.LineBasicMaterial({ color: 0x55606e }));
    wire.visible = false;
    this.wire = wire;
    this.meshGroup.add(solid, edges, wire);
    this.requestRender();
  }

  setWireframe(on) {
    if (this.wire) this.wire.visible = on;
    this.requestRender();
  }

  setVisible(which, on) {
    (which === 'mesh' ? this.meshGroup : this.pointsGroup).visible = on;
    if (which === 'points' && !on) this.marker.visible = false;
    this.requestRender();
  }

  // data = column arrays from the backend (id, x, y, z, t), all in m / C.
  setPoints(data, ramp) {
    this.clearGroup(this.pointsGroup);
    this.nodeData = data;
    const n = data.x.length;
    const pos = new Float32Array(n * 3);
    for (let i = 0; i < n; i++) {
      pos[3 * i] = data.x[i];
      pos[3 * i + 1] = data.y[i];
      pos[3 * i + 2] = data.z[i];
    }
    const geom = new THREE.BufferGeometry();
    geom.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geom.setAttribute('color', new THREE.BufferAttribute(new Float32Array(n * 3), 3));
    geom.computeBoundingSphere();
    const mat = new THREE.PointsMaterial({ size: this.pointSize, sizeAttenuation: false, vertexColors: true });
    this.points = new THREE.Points(geom, mat);
    this.pointsGroup.add(this.points);
    this.recolor(ramp);
  }

  recolor(ramp) {
    if (!this.points) return;
    const { t, minTemperature_C: minT, maxTemperature_C: maxT } = this.nodeData;
    const colors = this.points.geometry.getAttribute('color');
    const span = maxT - minT || 1;
    for (let i = 0; i < t.length; i++) {
      const c = sampleRamp(ramp, (t[i] - minT) / span);
      colors.setXYZ(i, c.r, c.g, c.b);
    }
    colors.needsUpdate = true;
    this.requestRender();
  }

  setPointSize(px) {
    this.pointSize = px;
    if (this.points) this.points.material.size = px;
    this.requestRender();
  }

  fitToView() {
    const box = new THREE.Box3();
    for (const g of [this.meshGroup, this.pointsGroup]) if (g.visible && g.children.length) box.expandByObject(g);
    if (box.isEmpty()) return;
    const sphere = box.getBoundingSphere(new THREE.Sphere());
    const r = sphere.radius || 1;
    const dist = r / Math.sin((this.camera.fov * Math.PI) / 360) * 1.15;
    const dir = new THREE.Vector3(0.9, -1.4, 1.0).normalize();
    this.camera.position.copy(sphere.center).addScaledVector(dir, dist);
    this.camera.near = dist / 1000;
    this.camera.far = dist * 100;
    this.camera.updateProjectionMatrix();
    this.controls.target.copy(sphere.center);
    this.controls.update();
    this.marker.scale.setScalar(r * 0.02);
    this.raycaster.params.Points.threshold = r * 0.015;
    this.requestRender();
  }

  handlePointer(event, isClick) {
    if (!this.points || !this.pointsGroup.visible) return;
    const rect = this.renderer.domElement.getBoundingClientRect();
    this.pointer.set(((event.clientX - rect.left) / rect.width) * 2 - 1, -((event.clientY - rect.top) / rect.height) * 2 + 1);
    this.raycaster.setFromCamera(this.pointer, this.camera);
    const hit = this.raycaster.intersectObject(this.points)[0];
    const index = hit ? hit.index : null;
    const at = { x: event.clientX - rect.left, y: event.clientY - rect.top };
    if (isClick && index !== null) {
      const p = this.points.geometry.getAttribute('position');
      this.marker.position.set(p.getX(index), p.getY(index), p.getZ(index));
      this.marker.visible = true;
      this.requestRender();
      this.onPick?.(index);
    } else {
      this.onHover?.(index, at);
    }
  }
}
