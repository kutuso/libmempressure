import io.kutu.mempressure.MemPressure;
import java.nio.file.Files;
import java.nio.file.Path;

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

        final int[] hits = {0};
        final int[] last = {-1};
        int handle = MemPressure.subscribe(
                level -> {
                    hits[0]++;
                    last[0] = level;
                });

        write(p, 60.0);
        long deadline = System.currentTimeMillis() + 3000;
        while (System.currentTimeMillis() < deadline && last[0] != 3) {
            Thread.sleep(5);
        }
        check(last[0] == 3, "callback saw CRITICAL");
        check(hits[0] >= 1, "callback fired");
        check(MemPressure.currentLevel() == 3, "currentLevel is CRITICAL");
        check(MemPressure.levelName(3).equals("critical"), "levelName");

        double[] psi = MemPressure.psi();
        check(psi.length == 6 && psi[0] == 60.0, "psi values");

        MemPressure.unsubscribe(handle);
        MemPressure.stop();
        Files.deleteIfExists(p);

        if (failures > 0) {
            System.out.println("JAVA TESTS: FAIL (" + failures + ")");
            System.exit(1);
        }
        System.out.println("JAVA TESTS: ALL PASS");
    }
}
