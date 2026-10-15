#pragma once

#include "controllermanager.h"

#include <QQuaternion>

class QTimer;

class QmlController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dualSense READ isDualSense CONSTANT)
    Q_PROPERTY(bool handheld READ isHandheld CONSTANT)
    Q_PROPERTY(bool steamVirtual READ isSteamVirtual CONSTANT)
    Q_PROPERTY(bool dualSenseEdge READ isDualSenseEdge CONSTANT)
    Q_PROPERTY(bool playStation READ isPS CONSTANT)
    // PS-WRAP: ข้อมูลจอยสำหรับหน้าสถานะใน UI (additive, อ่านจาก Controller/SDL ที่ upstream มีอยู่แล้ว)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString type READ type CONSTANT)
    Q_PROPERTY(QString vidpid READ vidpid CONSTANT)
    Q_PROPERTY(QString guid READ guid CONSTANT)
    Q_PROPERTY(bool hasLightbar READ hasLightbar CONSTANT)
    // PS-WRAP: สถานะ input สดสำหรับ overlay ขณะสตรีม (bitmask CHIAKI_CONTROLLER_BUTTON_*, แกน -1..1, trigger 0..1)
    Q_PROPERTY(uint buttons READ buttons NOTIFY inputChanged)
    Q_PROPERTY(qreal leftX READ leftX NOTIFY inputChanged)
    Q_PROPERTY(qreal leftY READ leftY NOTIFY inputChanged)
    Q_PROPERTY(qreal rightX READ rightX NOTIFY inputChanged)
    Q_PROPERTY(qreal rightY READ rightY NOTIFY inputChanged)
    Q_PROPERTY(qreal l2 READ l2 NOTIFY inputChanged)
    Q_PROPERTY(qreal r2 READ r2 NOTIFY inputChanged)
    // PS-WRAP: ทิศทางจอยจากไจโร+accel (quaternion จาก orientation tracker) — จอย 3D ใน overlay เอียงตาม
    Q_PROPERTY(QQuaternion orientation READ orientation NOTIFY inputChanged)

public:
    uint buttons() const { return controller ? controller->GetState().buttons : 0u; }
    qreal leftX() const { return controller ? controller->GetState().left_x / 32767.0 : 0.0; }
    qreal leftY() const { return controller ? controller->GetState().left_y / 32767.0 : 0.0; }
    qreal rightX() const { return controller ? controller->GetState().right_x / 32767.0 : 0.0; }
    qreal rightY() const { return controller ? controller->GetState().right_y / 32767.0 : 0.0; }
    qreal l2() const { return controller ? controller->GetState().l2_state / 255.0 : 0.0; }
    qreal r2() const { return controller ? controller->GetState().r2_state / 255.0 : 0.0; }
    QQuaternion orientation() const
    {
        if (!controller) return QQuaternion();
        const auto st = controller->GetState();
        return QQuaternion(st.orient_w, st.orient_x, st.orient_y, st.orient_z);
    }

signals:
    void inputChanged();

public:
    QString name() const { return controller ? controller->GetName() : QString(); }
    QString type() const { return controller ? controller->GetType() : QString(); }
    QString vidpid() const { return controller ? controller->GetVIDPIDString() : QString(); }
    QString guid() const { return controller ? controller->GetGUIDString() : QString(); }
    // "wired" | "full" | "medium" | "low" | "empty" | "unknown" — เรียกซ้ำได้ (ค่าเปลี่ยนตามเวลา)
    Q_INVOKABLE QString powerLevel() const
    {
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
        if (controller && controller->GetController()) {
            switch (SDL_JoystickCurrentPowerLevel(SDL_GameControllerGetJoystick(controller->GetController()))) {
            case SDL_JOYSTICK_POWER_WIRED: return QStringLiteral("wired");
            case SDL_JOYSTICK_POWER_MAX:
            case SDL_JOYSTICK_POWER_FULL: return QStringLiteral("full");
            case SDL_JOYSTICK_POWER_MEDIUM: return QStringLiteral("medium");
            case SDL_JOYSTICK_POWER_LOW: return QStringLiteral("low");
            case SDL_JOYSTICK_POWER_EMPTY: return QStringLiteral("empty");
            default: break;
            }
        }
#endif
        return QStringLiteral("unknown");
    }
    // PS-WRAP: พรีวิวแรงสั่นในหน้า Controllers — สั่น 3 จังหวะ (25/50/75% แบบที่เกมสั่ง) คูณ boost แล้ว
    Q_INVOKABLE void previewRumble(qreal boost);
    Q_INVOKABLE void stopRumble();
    // PS-WRAP: ทดสอบไฟจอย — restoreLightbar() คืนสีตั้งต้นของ SDL ตามลำดับผู้เล่น (ใช้ตอนไม่ได้สตรีม)
    Q_INVOKABLE void setLightbar(const QColor &color);
    Q_INVOKABLE void restoreLightbar();
    bool hasLightbar() const
    {
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
        return controller && controller->GetController() && SDL_GameControllerHasLED(controller->GetController());
#else
        return false;
#endif
    }

    QmlController(Controller *controller, uint32_t shortcut, QObject *target, QObject *parent = nullptr);
    ~QmlController();

    bool isDualSense() const;
    bool isHandheld() const;
    bool isSteamVirtual() const;
    bool isDualSenseEdge() const;
    bool isPS() const;
    void setEscapeShortcut(uint32_t shortcut) { escape_shortcut = shortcut; };
    QString GetGUID() const;
    QString GetVIDPID() const;

private:
    void sendKey(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

    QObject *target = {};
    uint32_t escape_shortcut = 0;
    uint32_t old_buttons = 0;
    bool old_l2 = false;   // PS-WRAP: L2/R2 เป็นแกน analog → ส่งเป็นคีย์ F13/F14 (ช่อง PIN แบบ PS5: L2=8, R2=6)
    bool old_r2 = false;
    Controller *controller = {};
    QTimer *repeat_timer = {};
    int repeat_running = 0;
    Qt::Key pressed_key = Qt::Key_unknown;
    QTimer *rumble_timer = {};
    int rumble_step = 0;
    qreal rumble_boost = 1.0;
};
