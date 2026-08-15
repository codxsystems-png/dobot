// ═══════════════════════════════════════════════════════════════════════════════
// Test: PIDController — anti-windup, steady-state elimination, reset, dt guard.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include <cmath>
#include "infrastructure/gantry/pid_controller.h"

class TestPidController : public QObject
{
    Q_OBJECT
private slots:
    void testNonPositiveDtFallsBackToProportionalOnly()
    {
        gantry::PIDController pid(2.0, 5.0, 3.0, -100.0, 100.0);
        double output = pid.compute(10.0, 0.0, 0.0);
        QCOMPARE(output, 20.0); // Kp*error only, clamped
    }

    void testResetClearsIntegratorState()
    {
        // Ki-only, wide output range so clamping/anti-windup never interferes.
        gantry::PIDController pid(0.0, 1.0, 0.0, -1000.0, 1000.0);
        for (int i = 0; i < 100; ++i) {
            pid.compute(10.0, 0.0, 0.02); // builds integral toward 100 * 10 * 0.02 = 20
        }
        pid.reset();
        double output = pid.compute(0.0, 0.0, 0.02); // error 0 right after reset
        QCOMPARE(output, 0.0);
    }

    void testAntiWindupPreventsStickingAfterSaturation()
    {
        gantry::PIDController pid(1.0, 2.0, 0.0, -10.0, 10.0);

        // Sustained huge error — output saturates immediately and, without
        // anti-windup, the integral would keep growing far past what's
        // achievable while pegged at the output limit.
        for (int i = 0; i < 500; ++i) {
            pid.compute(1000.0, 0.0, 0.02);
        }

        // Error resolves (measurement catches up to setpoint) — with
        // anti-windup the output should snap back immediately instead of
        // staying pegged near the max while a runaway integral bleeds off.
        double output = pid.compute(0.0, 1000.0, 0.02);
        QVERIFY(output < 5.0);
    }

    void testIntegralEliminatesSteadyStateError()
    {
        // Plant: velocity = clamp(output, bounds) - frictionBias;
        //        position += velocity * dt
        // A constant disturbance (frictionBias) that pure P control can only
        // offset by carrying a permanent nonzero error; PI eliminates it.
        const double target = 100.0;
        const double frictionBias = 5.0;
        const double dt = 0.02;

        // P-only: settles with a real, persistent steady-state error.
        {
            gantry::PIDController pid(0.5, 0.0, 0.0, -50.0, 50.0);
            double position = 0.0;
            for (int i = 0; i < 2000; ++i) {
                double error = target - position;
                double output = pid.compute(error, position, dt);
                double velocity = output - frictionBias;
                position += velocity * dt;
            }
            QVERIFY(std::abs(target - position) > 1.0);
        }

        // PI: the integral term accumulates the bias needed to cancel the
        // disturbance, driving steady-state error toward zero.
        {
            gantry::PIDController pid(0.5, 0.3, 0.0, -50.0, 50.0);
            double position = 0.0;
            for (int i = 0; i < 6000; ++i) {
                double error = target - position;
                double output = pid.compute(error, position, dt);
                double velocity = output - frictionBias;
                position += velocity * dt;
            }
            QVERIFY(std::abs(target - position) < 0.5);
        }
    }
};

QTEST_APPLESS_MAIN(TestPidController)
#include "test_pid_controller.moc"
