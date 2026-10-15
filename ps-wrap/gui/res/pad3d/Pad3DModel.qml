import QtQuick
import QtQuick3D

// PS-WRAP: สร้างจาก assets-src/dualsense/make_pad3d.py — อย่าแก้ตรงนี้ (แก้สคริปต์แล้วสร้างใหม่)
// Model: "Playstation 5 Dualsense" by AHarmlessPotato, CC-BY-4.0 (sketchfab.com/3d-models/playstation-5-dualsense-878c1f882808477ab81c2fe86d5a3936)

Node {
    id: node

    // PS-WRAP: input สด (ControllerOverlay3D.qml ตั้งค่า) — ปุ่ม = bitmask CHIAKI_CONTROLLER_BUTTON_* · แกน -1..1 · ไก 0..1
    property int buttons: 0
    property real lx: 0
    property real ly: 0
    property real rx: 0
    property real ry: 0
    property real l2: 0
    property real r2: 0
    property color lightColor: "transparent"   // สีไฟที่เครื่องสั่ง · โปร่งใส/มืด = ไฟดับ
    readonly property real press: 0.03           // ปุ่มยุบลง (หน่วย mesh) — overlay เล็ก ต้องยุบให้เห็น
    // ปุ่มที่กดอยู่ = ชมพูเรืองแสง สีเดียวกับ overlay 2D (ds-B*.svg #E8185D) — ยุบอย่างเดียวมองไม่ออกตอนจอยเล็ก
    PrincipledMaterial {
        id: pressed_material
        baseColor: "#E8185D"
        emissiveFactor: Qt.vector3d(1.4, 0.15, 0.5)
        roughness: 0.4
    }
    // ไก L2/R2: เรืองแสงตามแรงกด
    PrincipledMaterial {
        id: l2_material
        baseColor: node.l2 > 0.02 ? "#E8185D" : "#26272e"
        emissiveFactor: Qt.vector3d(1.4 * node.l2, 0.15 * node.l2, 0.5 * node.l2)
        roughness: 0.5
    }
    PrincipledMaterial {
        id: r2_material
        baseColor: node.r2 > 0.02 ? "#E8185D" : "#26272e"
        emissiveFactor: Qt.vector3d(1.4 * node.r2, 0.15 * node.r2, 0.5 * node.r2)
        roughness: 0.5
    }
    readonly property bool lightOn: lightColor.a > 0 && (lightColor.r + lightColor.g + lightColor.b) > 0.3

    // Resources
    property url textureData: "maps/textureData.jpg"
    property url textureData5: "maps/textureData5.jpg"
    property url textureData7: "maps/textureData7.jpg"
    property url textureData23: "maps/textureData23.jpg"
    property url textureData9: "maps/textureData9.jpg"
    property url textureData21: "maps/textureData21.jpg"
    property url textureData12: "maps/textureData12.jpg"
    property url textureData14: "maps/textureData14.jpg"
    property url textureData19: "maps/textureData19.jpg"
    property url textureData16: "maps/textureData16.jpg"
    Texture {
        id: _0_texture
        generateMipmaps: true
        mipFilter: Texture.Linear
        source: node.textureData5
    }
    Texture {
        id: _9_texture
        generateMipmaps: true
        mipFilter: Texture.Linear
        source: node.textureData
    }
    Texture {
        id: _8_texture
        generateMipmaps: true
        mipFilter: Texture.Linear
        source: node.textureData23
    }
    Texture {
        id: _7_texture
        generateMipmaps: true
        mipFilter: Texture.Linear
        source: node.textureData21
    }
    Texture {
        id: _6_texture
        generateMipmaps: true
        mipFilter: Texture.Linear
        source: node.textureData19
    }
    Texture {
        id: _5_texture
        generateMipmaps: true
        mipFilter: Texture.Linear
        source: node.textureData16
    }
    Texture {
        id: _1_texture
        generateMipmaps: true
        mipFilter: Texture.Linear
        source: node.textureData7
    }
    Texture {
        id: _2_texture
        generateMipmaps: true
        mipFilter: Texture.Linear
        source: node.textureData9
    }
    Texture {
        id: _3_texture
        generateMipmaps: true
        mipFilter: Texture.Linear
        source: node.textureData12
    }
    Texture {
        id: _4_texture
        generateMipmaps: true
        mipFilter: Texture.Linear
        source: node.textureData14
    }
    PrincipledMaterial {
        id: node1011_material
        objectName: "1011"
        baseColorMap: _0_texture
        roughnessMap: _1_texture
        metalness: 0
        roughness: 1
        normalMap: _2_texture
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: lightbar_material
        objectName: "lightbar"
        baseColor: "#ff272730"
        roughness: 0.3499999940395355
        emissiveMap: _9_texture
        emissiveFactor: node.lightOn ? Qt.vector3d(node.lightColor.r * 6, node.lightColor.g * 6, node.lightColor.b * 6) : Qt.vector3d(0.04, 0.04, 0.05)
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: node1002_material
        objectName: "1002"
        baseColorMap: _6_texture
        roughnessMap: _7_texture
        metalness: 0
        roughness: 1
        normalMap: _8_texture
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: node1001_material
        objectName: "1001"
        baseColorMap: _3_texture
        roughnessMap: _4_texture
        metalness: 0
        roughness: 1
        normalMap: _5_texture
        alphaMode: PrincipledMaterial.Opaque
    }

    // Nodes:
    Node {
        id: dualSense
        objectName: "DualSense"
        position: Qt.vector3d(1.39475e-05, 4.34319e-09, -1.40797e-06)
        rotation: Qt.quaternion(-1.41638e-16, -3.09561e-24, 1, -2.18557e-08)
        scale: Qt.vector3d(0.195684, 0.195684, 0.195684)
        Model {
            id: body
            objectName: "body"
            source: "meshes/body_mesh.mesh"
            materials: [
                node1011_material,
                node1001_material,
                node1002_material
            ]
        }
        Model {
            id: lightbar
            objectName: "lightbar"
            position: Qt.vector3d(0, 0.454195, 0.238755)
            source: "meshes/lightbar_mesh.mesh"
            materials: [
                lightbar_material
            ]
        }
        Model {
            id: touchpad
            objectName: "touchpad"
            position: Qt.vector3d(0, 0.410978, 0.25017 - ((node.buttons & 16384) ? node.press : 0))
            source: "meshes/touchpad_mesh.mesh"
            materials: [
                (node.buttons & 16384) ? pressed_material : node1011_material
            ]
        }
        Model {
            id: stick_L
            objectName: "stick_L"
            position: Qt.vector3d(-0.317647, -0.0012585, 0.280419 - ((node.buttons & 1024) ? node.press : 0))
            eulerRotation: Qt.vector3d(node.ly * 30, -node.lx * 30, 0)
            source: "meshes/stick_L_mesh.mesh"
            materials: [
                (node.buttons & 1024) ? pressed_material : node1011_material,
                (node.buttons & 1024) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: stick_R
            objectName: "stick_R"
            position: Qt.vector3d(0.317647, -0.0012585, 0.280419 - ((node.buttons & 2048) ? node.press : 0))
            eulerRotation: Qt.vector3d(node.ry * 30, -node.rx * 30, 0)
            source: "meshes/stick_R_mesh.mesh"
            materials: [
                (node.buttons & 2048) ? pressed_material : node1011_material,
                (node.buttons & 2048) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: dpad_up
            objectName: "dpad_up"
            position: Qt.vector3d(-0.61999, 0.375888, 0.285744 - ((node.buttons & 64) ? node.press : 0))
            source: "meshes/dpad_up_mesh.mesh"
            materials: [
                (node.buttons & 64) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: dpad_down
            objectName: "dpad_down"
            position: Qt.vector3d(-0.626464, 0.176759, 0.285744 - ((node.buttons & 128) ? node.press : 0))
            source: "meshes/dpad_down_mesh.mesh"
            materials: [
                (node.buttons & 128) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: dpad_left
            objectName: "dpad_left"
            position: Qt.vector3d(-0.722816, 0.280166, 0.285744 - ((node.buttons & 16) ? node.press : 0))
            source: "meshes/dpad_left_mesh.mesh"
            materials: [
                (node.buttons & 16) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: dpad_right
            objectName: "dpad_right"
            position: Qt.vector3d(-0.524137, 0.273624, 0.285744 - ((node.buttons & 32) ? node.press : 0))
            source: "meshes/dpad_right_mesh.mesh"
            materials: [
                (node.buttons & 32) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: btn_triangle
            objectName: "btn_triangle"
            position: Qt.vector3d(0.622509, 0.423149, 0.296989 - ((node.buttons & 8) ? node.press : 0))
            source: "meshes/btn_triangle_mesh.mesh"
            materials: [
                (node.buttons & 8) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: btn_circle
            objectName: "btn_circle"
            position: Qt.vector3d(0.769672, 0.276092, 0.282053 - ((node.buttons & 2) ? node.press : 0))
            source: "meshes/btn_circle_mesh.mesh"
            materials: [
                (node.buttons & 2) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: btn_cross
            objectName: "btn_cross"
            position: Qt.vector3d(0.622509, 0.128752, 0.285236 - ((node.buttons & 1) ? node.press : 0))
            source: "meshes/btn_cross_mesh.mesh"
            materials: [
                (node.buttons & 1) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: btn_square
            objectName: "btn_square"
            position: Qt.vector3d(0.476362, 0.275982, 0.304374 - ((node.buttons & 4) ? node.press : 0))
            source: "meshes/btn_square_mesh.mesh"
            materials: [
                (node.buttons & 4) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: l1
            objectName: "L1"
            position: Qt.vector3d(-0.618773, 0.577204 - ((node.buttons & 256) ? node.press : 0), 0.122191)
            source: "meshes/l1_mesh.mesh"
            materials: [
                (node.buttons & 256) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: r1
            objectName: "R1"
            position: Qt.vector3d(0.618773, 0.577207 - ((node.buttons & 512) ? node.press : 0), 0.122191)
            source: "meshes/r1_mesh.mesh"
            materials: [
                (node.buttons & 512) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: l2
            objectName: "L2"
            position: Qt.vector3d(-0.61384, 0.64655, 0.00449407)
            eulerRotation.x: -node.l2 * 26
            source: "meshes/l2_mesh.mesh"
            materials: [ l2_material ]
        }
        Model {
            id: r2
            objectName: "R2"
            position: Qt.vector3d(0.613841, 0.64655, 0.00449407)
            eulerRotation.x: -node.r2 * 26
            source: "meshes/r2_mesh.mesh"
            materials: [ r2_material ]
        }
        Model {
            id: btn_create
            objectName: "btn_create"
            position: Qt.vector3d(-0.466919, 0.493559, 0.293674 - ((node.buttons & 8192) ? node.press : 0))
            source: "meshes/btn_create_mesh.mesh"
            materials: [
                (node.buttons & 8192) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: btn_options
            objectName: "btn_options"
            position: Qt.vector3d(0.466919, 0.493559, 0.293674 - ((node.buttons & 4096) ? node.press : 0))
            source: "meshes/btn_options_mesh.mesh"
            materials: [
                (node.buttons & 4096) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: btn_ps
            objectName: "btn_ps"
            position: Qt.vector3d(-0.000106849, 0.0196966, 0.257484 - ((node.buttons & 32768) ? node.press : 0))
            source: "meshes/btn_ps_mesh.mesh"
            materials: [
                (node.buttons & 32768) ? pressed_material : node1001_material
            ]
        }
        Model {
            id: btn_mute
            objectName: "btn_mute"
            position: Qt.vector3d(0, -0.0953743, 0.211985)
            source: "meshes/btn_mute_mesh.mesh"
            materials: [
                node1001_material
            ]
        }
    }

    // Animations:
}
