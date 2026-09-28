import io.kutu.mempressure.MemPressure;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.atomic.AtomicInteger;

public class MemPressureTest {
    static int failures = 0;

    static void check(boolean cond, String what) {
        if (!cond) {
            System.out.println("FAIL: " + what);
            failures++;
        }
    }

    static void write(Path p, double some10) throws Exception {
        Files.writeString(
                p,
                String.format(
                        "some avg10=%.2f avg60=0.00 avg300=0.00 total=0%n"
                                + "full avg10=0.00 avg60=0.00 avg300=0.00 total=0%n",
                        some10));
    }

    public static void main(String[] args) throws Exception {
        Path p = Files.createTempFile("mp_test_java", ".psi");
        write(p, 0.0);

        MemPressure.Config cfg = new MemPressure.Config();
        cfg.psiPath(p.toString());
        cfg.intervalSec = 0.02;
        cfg.hysteresis = 1;

        MemPressure.start(cfg);
        check(MemPressure.currentLevel() == 0, "initial level is NONE");

        final AtomicInteger hits = new AtomicInteger();
        final AtomicInteger last = new AtomicInteger(-1);
        int handle = MemPressure.subscribe(
                level -> {
                    hits.incrementAndGet();
                    last.set(level);
                });

        write(p, 60.0);
        long deadline = System.currentTimeMillis() + 3000;
        while (System.currentTimeMillis() < deadline && last.get() != 3) {
            Thread.sleep(5);
        }
        check(last.get() == 3, "callback saw CRITICAL");
        check(hits.get() >= 1, "callback fired");
        check(MemPressure.currentLevel() == 3, "currentLevel is CRITICAL");
        check(MemPressure.levelName(3).equals("critical"), "levelName");

        double[] psi = MemPressure.psi();
        check(psi.length == 6 && psi[0] == 60.0, "psi values");

        MemPressure.unsubscribe(handle);
        int after = hits.get();
        write(p, 0.0);
        Thread.sleep(150);
        check(hits.get() == after, "no callbacks after unsubscribe");

        MemPressure.stop();
        try {
            MemPressure.psi();
            check(false, "psi() after stop should throw");
        } catch (MemPressure.MemPressureException expected) {
            // the monitor is stopped; the JNI layer must report it
        }
        Files.deleteIfExists(p);

        if (failures > 0) {
            System.out.println("JAVA TESTS: FAIL (" + failures + ")");
            System.exit(1);
        }
        System.out.println("JAVA TESTS: ALL PASS");
    }
}
