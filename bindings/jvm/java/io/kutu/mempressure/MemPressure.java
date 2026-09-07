package io.kutu.mempressure;

/** Subscribe a JVM application to kernel memory-pressure events (PSI). */
public final class MemPressure {
    public enum Level {
        NONE(0),
        LOW(1),
        MODERATE(2),
        CRITICAL(3);

        public final int code;

        Level(int code) {
            this.code = code;
        }

        public static Level from(int code) {
            for (Level level : values()) {
                if (level.code == code) {
                    return level;
                }
            }
            throw new IllegalArgumentException("unknown level: " + code);
        }
    }

    /** Fired from the monitor thread; keep it fast, do not block. */
    public interface Callback {
        void onPressure(int level);
    }

    public static final class Config {
        public double low = 5.0;
        public double moderate = 15.0;
        public double critical = 40.0;
        public double intervalSec = 0.5;
        public int hysteresis = 2;
        public String psiPath = "/proc/pressure/memory";

        public Config psiPath(String value) {
            psiPath = value;
            return this;
        }
    }

    public static class MemPressureException extends RuntimeException {
        public MemPressureException(String message) {
            super(message);
        }
    }

    static {
        System.loadLibrary("mempressure_jni");
    }

    private MemPressure() {}

    public static native void start(Config cfg);

    public static native void stop();

    public static native int currentLevel();

    public static native double[] psi();

    public static native int subscribe(Callback cb);

    public static native void unsubscribe(int handle);

    public static String levelName(int code) {
        switch (code) {
            case 0:
                return "none";
            case 1:
                return "low";
            case 2:
                return "moderate";
            case 3:
                return "critical";
            default:
                return "unknown";
        }
    }
}
