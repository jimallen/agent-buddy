import * as THREE from './node_modules/three/build/three.module.js';

// Pi's character: a round light-blue desk buddy with a glowing white π on its chest.
// Same render contract as OpenClaw's model: 240x224 frames on black, poses addressed by
// {track, step, blink}.
const WIDTH = 240;
const HEIGHT = 224;
const SCALE = 18;
const BLUE_START = new THREE.Color('#8FDBFF');
const BLUE_END = new THREE.Color('#2B5FD9');
const DARK = new THREE.MeshBasicMaterial({color: '#06121F'});
const GLOW = new THREE.MeshBasicMaterial({color: '#AEF3FF'});
const EMBLEM = new THREE.MeshBasicMaterial({color: '#F4FAFF'});

const sx = value => (value - 60) / SCALE;
const sy = value => (60 - value) / SCALE;

function shellMaterial() {
  const material = new THREE.ShaderMaterial({
    side: THREE.DoubleSide,
    uniforms: {
      startColor: {value: BLUE_START},
      endColor: {value: BLUE_END},
      keyDirection: {value: new THREE.Vector3(-0.4, 0.55, 1).normalize()},
    },
    vertexShader: `
      varying vec3 vNormalView;
      varying vec3 vPosition;
      void main() {
        vPosition = position;
        vNormalView = normalize(normalMatrix * normal);
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
      }
    `,
    fragmentShader: `
      uniform vec3 startColor;
      uniform vec3 endColor;
      uniform vec3 keyDirection;
      varying vec3 vNormalView;
      varying vec3 vPosition;
      void main() {
        float gradient = clamp(round(clamp((3.4 - vPosition.y) / 6.8, 0.0, 1.0) * 1.5), 0.0, 1.0);
        vec3 base = mix(startColor, endColor, gradient);
        float diffuse = .97 + .03 * step(0.5, dot(normalize(vNormalView), keyDirection));
        gl_FragColor = vec4(base * diffuse, 1.0);
      }
    `,
  });
  material.toneMapped = false;
  return material;
}

// The body sits in the same 120-wide art grid as the other characters: a soft dome that
// is wider than tall, flat along the bottom.
function bodyShape() {
  const shape = new THREE.Shape();
  shape.moveTo(sx(18), sy(90));
  shape.bezierCurveTo(sx(18), sy(98), sx(30), sy(102), sx(44), sy(102));
  shape.lineTo(sx(76), sy(102));
  shape.bezierCurveTo(sx(90), sy(102), sx(102), sy(98), sx(102), sy(90));
  shape.bezierCurveTo(sx(104), sy(44), sx(90), sy(12), sx(60), sy(12));
  shape.bezierCurveTo(sx(30), sy(12), sx(16), sy(44), sx(18), sy(90));
  return shape;
}

function extrude(shape, depth, bevel) {
  const geometry = new THREE.ExtrudeGeometry(shape, {
    depth, bevelEnabled: true, bevelThickness: bevel, bevelSize: bevel, bevelSegments: 10,
    curveSegments: 48,
  });
  geometry.translate(0, 0, -depth / 2);
  geometry.computeVertexNormals();
  return geometry;
}

function createBody(material) {
  const mesh = new THREE.Mesh(extrude(bodyShape(), .9, .5), material);
  mesh.name = 'body-shell';
  return mesh;
}

// The π chest emblem as one outline: the top bar plus its two legs.
function piEmblemShape() {
  const shape = new THREE.Shape();
  shape.moveTo(-.62, .56);
  shape.lineTo(.62, .56);
  shape.bezierCurveTo(.66, .56, .68, .5, .66, .4);
  shape.lineTo(.62, .32);
  shape.lineTo(.4, .32);
  shape.lineTo(.34, -.4);
  shape.bezierCurveTo(.33, -.5, .28, -.54, .2, -.54);
  shape.lineTo(.08, -.54);
  shape.bezierCurveTo(.0, -.54, -.05, -.5, -.04, -.4);
  shape.lineTo(.1, .32);
  shape.lineTo(-.1, .32);
  shape.lineTo(-.16, -.4);
  shape.bezierCurveTo(-.17, -.5, -.22, -.54, -.3, -.54);
  shape.lineTo(-.42, -.54);
  shape.bezierCurveTo(-.5, -.54, -.55, -.5, -.54, -.4);
  shape.lineTo(-.4, .32);
  shape.lineTo(-.62, .32);
  shape.bezierCurveTo(-.68, .32, -.7, .42, -.68, .48);
  shape.lineTo(-.62, .56);
  return shape;
}

function createEmblem() {
  // Front of the domed body, low on the chest, behind nothing else.
  const mesh = new THREE.Mesh(extrude(piEmblemShape(), .08, .03), EMBLEM);
  mesh.position.set(0, sy(64), 1.46);
  mesh.scale.setScalar(1.28);
  mesh.name = 'pi-emblem';
  return mesh;
}

function armShape(side) {
  const shape = new THREE.Shape();
  const x = side < 0 ? 12 : 108;
  shape.moveTo(sx(x - 3 * side * -1), sy(48));
  shape.bezierCurveTo(sx(x - 7), sy(52), sx(x - 7), sy(66), sx(x - 2), sy(72));
  shape.bezierCurveTo(sx(x + 3), sy(76), sx(x + 6), sy(72), sx(x + 6), sy(60));
  shape.bezierCurveTo(sx(x + 6), sy(50), sx(x + 3 * side), sy(46), sx(x), sy(46));
  return shape;
}

function createArm(side, material) {
  const anchor = new THREE.Vector3(sx(side < 0 ? 20 : 100), sy(50), .1);
  const geometry = extrude(armShape(side), .45, .22);
  geometry.translate(-anchor.x, -anchor.y, 0);
  const pivot = new THREE.Group();
  pivot.name = side < 0 ? 'left-arm-pivot' : 'right-arm-pivot';
  pivot.position.copy(anchor);
  const mesh = new THREE.Mesh(geometry, material);
  mesh.name = side < 0 ? 'left-arm' : 'right-arm';
  pivot.add(mesh);
  return pivot;
}

function createFoot(side, material) {
  const center = side < 0 ? 46 : 74;
  const shape = new THREE.Shape();
  shape.moveTo(sx(center - 7), sy(101));
  shape.lineTo(sx(center + 7), sy(101));
  shape.lineTo(sx(center + 7), sy(112));
  shape.bezierCurveTo(sx(center + 7), sy(115), sx(center + 5), sy(116), sx(center), sy(116));
  shape.bezierCurveTo(sx(center - 5), sy(115), sx(center - 7), sy(115), sx(center - 7), sy(112));
  shape.closePath();
  const anchor = new THREE.Vector3(sx(center), sy(103), -.05);
  const geometry = extrude(shape, .5, .18);
  geometry.translate(-anchor.x, -anchor.y, 0);
  const pivot = new THREE.Group();
  pivot.name = side < 0 ? 'left-foot-pivot' : 'right-foot-pivot';
  pivot.position.copy(anchor);
  pivot.add(new THREE.Mesh(geometry, material));
  return pivot;
}

function createEye(side) {
  const pivot = new THREE.Group();
  pivot.name = side < 0 ? 'left-eye-pivot' : 'right-eye-pivot';
  pivot.position.set(sx(side < 0 ? 42 : 78), sy(34), 1.38);
  const sclera = new THREE.Mesh(new THREE.SphereGeometry(.36, 32, 20), DARK);
  sclera.scale.z = .3;
  sclera.name = 'sclera';
  const pupil = new THREE.Mesh(new THREE.SphereGeometry(.15, 24, 16), GLOW);
  pupil.position.set(.05, .05, .33);
  pupil.scale.z = .38;
  pupil.name = 'pupil';
  pivot.add(sclera, pupil);
  return {pivot, sclera, pupil};
}

export function createPiScene(canvas) {
  const renderer = new THREE.WebGLRenderer({
    canvas, antialias: true, alpha: false, preserveDrawingBuffer: true,
    powerPreference: 'high-performance',
  });
  renderer.setSize(WIDTH, HEIGHT, false);
  renderer.setPixelRatio(1);
  renderer.setClearColor(0x000000, 1);
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.NoToneMapping;

  const scene = new THREE.Scene();
  const aspect = WIDTH / HEIGHT;
  const camera = new THREE.OrthographicCamera(-4 * aspect, 4 * aspect, 4, -4, .1, 30);
  camera.position.set(0, 0, 12);
  camera.lookAt(0, 0, 0);

  scene.add(new THREE.HemisphereLight(0xffffff, 0x0d1626, 1.55));
  const key = new THREE.DirectionalLight(0xffffff, 2.2);
  key.position.set(-4, 6, 8);
  scene.add(key);
  const fill = new THREE.DirectionalLight(0xbfd8ff, .65);
  fill.position.set(5, 1, 5);
  scene.add(fill);
  const rim = new THREE.DirectionalLight(0x7df9ff, .45);
  rim.position.set(0, 4, -6);
  scene.add(rim);

  const shell = shellMaterial();
  const root = new THREE.Group();
  root.name = 'pi-root';
  root.position.y = .08;
  root.scale.set(.82, .87, 1);

  const body = createBody(shell);
  const emblem = createEmblem();
  const leftArm = createArm(-1, shell);
  const rightArm = createArm(1, shell);
  const leftFoot = createFoot(-1, shell);
  const rightFoot = createFoot(1, shell);
  const leftEye = createEye(-1);
  const rightEye = createEye(1);
  root.add(leftFoot, rightFoot, body, emblem, leftArm, rightArm, leftEye.pivot, rightEye.pivot);
  scene.add(root);

  const neutral = {
    rootPosition: root.position.clone(),
    rootScale: root.scale.clone(),
    emblemPosition: emblem.position.clone(),
    leftArmPosition: leftArm.position.clone(),
    rightArmPosition: rightArm.position.clone(),
    leftFootPosition: leftFoot.position.clone(),
    rightFootPosition: rightFoot.position.clone(),
    leftArm: leftArm.rotation.clone(), rightArm: rightArm.rotation.clone(),
    leftFoot: leftFoot.rotation.clone(), rightFoot: rightFoot.rotation.clone(),
  };

  function reset() {
    root.position.copy(neutral.rootPosition);
    root.rotation.set(0, 0, 0);
    root.scale.copy(neutral.rootScale);
    emblem.position.copy(neutral.emblemPosition);
    leftArm.position.copy(neutral.leftArmPosition);
    rightArm.position.copy(neutral.rightArmPosition);
    leftFoot.position.copy(neutral.leftFootPosition);
    rightFoot.position.copy(neutral.rightFootPosition);
    leftArm.rotation.copy(neutral.leftArm);
    rightArm.rotation.copy(neutral.rightArm);
    leftFoot.rotation.copy(neutral.leftFoot);
    rightFoot.rotation.copy(neutral.rightFoot);
    for (const eye of [leftEye, rightEye]) {
      eye.pivot.position.x = sx(eye === leftEye ? 42 : 78);
      eye.pivot.position.y = sy(34);
      eye.sclera.scale.set(1, 1, .3);
      eye.pupil.scale.set(1, 1, .38);
      eye.pupil.position.set(.05, .05, .33);
      eye.pupil.visible = true;
    }
  }

  function applyPose(track, step, blinkLevel) {
    reset();
    const t = Math.max(0, Math.min(1, step / 23));
    const eased = t * t * (3 - 2 * t);
    const directions = {
      right: [-38, 0], left: [38, 0], up: [0, 22], down: [0, -22],
      up_right: [-32, 18], up_left: [32, 18],
      down_right: [-16, -22], down_left: [16, -22],
    };
    if (directions[track]) {
      const [yaw, pitch] = directions[track];
      const horizontalTurn = yaw / 38;
      const followThrough = Math.min(1, eased + .12 * Math.sin(Math.PI * t));
      root.rotation.y = THREE.MathUtils.degToRad(yaw * eased);
      root.rotation.x = THREE.MathUtils.degToRad(pitch * eased);
      root.rotation.z = THREE.MathUtils.degToRad(-2.2 * horizontalTurn * followThrough);
      root.position.y += .045 * Math.sin(Math.PI * t);
      leftArm.rotation.z = -(.09 + .14 * Math.max(horizontalTurn, 0)) * followThrough;
      rightArm.rotation.z = (.09 + .14 * Math.max(-horizontalTurn, 0)) * followThrough;
      leftEye.pupil.position.x -= .045 * horizontalTurn * eased;
      rightEye.pupil.position.x -= .045 * horizontalTurn * eased;
      if (pitch) {
        leftEye.pupil.position.y += .04 * (pitch / 22) * eased;
        rightEye.pupil.position.y += .04 * (pitch / 22) * eased;
      }
      if (track === 'right') {
        // The wave: Pi turns to face you and its right arm swings up and wags.
        const waveLift = THREE.MathUtils.smoothstep(t, 0, .28);
        const waveProgress = Math.max(0, (t - .28) / .72);
        const wristWave = Math.sin(4 * Math.PI * waveProgress);
        root.rotation.y = THREE.MathUtils.degToRad(-8 * waveLift);
        rightArm.rotation.z += .62 * waveLift + .3 * wristWave * waveLift;
        rightArm.position.y += .5 * waveLift;
        leftArm.rotation.z -= .08 * waveLift;
        leftEye.pupil.position.x += .035 * waveLift;
        rightEye.pupil.position.x += .035 * waveLift;
      }
    } else if (track === 'surprise') {
      const spring = Math.sin(Math.PI * t) * Math.exp(-1.35 * t);
      root.scale.copy(neutral.rootScale).multiplyScalar(1 - .15 * spring);
      root.position.y -= .18 * spring;
      leftArm.rotation.z = -.5 * spring;
      rightArm.rotation.z = .5 * spring;
      emblem.position.y += .06 * spring;
      for (const eye of [leftEye, rightEye]) eye.sclera.scale.multiplyScalar(1 + .28 * spring);
    } else if (track === 'working') {
      // Typing: the body rocks gently and the stub arms alternate like little hands.
      const phase = step ? (step - 1) / 22 * Math.PI * 2 : 0;
      const stride = Math.sin(phase);
      const bounce = Math.abs(stride);
      root.rotation.y = THREE.MathUtils.degToRad(5 * stride);
      root.rotation.z = THREE.MathUtils.degToRad(-1.8 * stride);
      root.position.y += .035 + .085 * bounce;
      leftArm.rotation.x = -.5 * Math.max(stride, 0);
      rightArm.rotation.x = -.5 * Math.max(-stride, 0);
      emblem.position.z = 1.46 + .025 * bounce;
    } else if (track === 'complete') {
      // A satisfied hop with both stub arms thrown up, eyes happily shut.
      root.position.y += .16 * Math.sin(Math.PI * t);
      leftArm.rotation.z = -.72 * eased;
      rightArm.rotation.z = .72 * eased;
      leftArm.position.y += .3 * eased;
      rightArm.position.y += .3 * eased;
    } else if (track === 'attention' || track === 'attention_alternate') {
      // A lean to one side, that side's arm up, that eye wide.
      const side = track === 'attention' ? -1 : 1;
      root.rotation.z = THREE.MathUtils.degToRad(side * 8 * eased);
      (side < 0 ? leftArm : rightArm).rotation.z = side * .62 * eased;
      (side < 0 ? leftEye : rightEye).sclera.scale.multiplyScalar(1 + .22 * eased);
    }

    let expressionOpen = 1;
    if (track === 'working') expressionOpen = step ? .84 : 1;
    if (track === 'complete') expressionOpen = 1 - .72 * eased;
    const blinkOpen = [1, .75, .5, .25, .035][blinkLevel] ?? 1;
    const openness = Math.max(.035, expressionOpen * blinkOpen);
    for (const eye of [leftEye, rightEye]) {
      eye.sclera.scale.y *= openness;
      eye.pupil.scale.y *= openness;
      eye.pupil.visible = openness > .1;
    }
  }

  function render(pose) {
    applyPose(pose.track, pose.step, pose.blink);
    scene.updateMatrixWorld(true);
    renderer.render(scene, camera);
  }

  return {scene, camera, renderer, root, render};
}
