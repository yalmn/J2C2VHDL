public class Ggt {
  public static int ggt(int a, int b) {
    int u;
    int v;
    int t;
    int i;

    u = a;
    if (u < 0) {
      u = -u;
    }
    v = b;
    if (v < 0) {
      v = -v;
    }

    for (i = 0; i < 64; i = i + 1) {
      if (v != 0) {
        t = u % v;
        u = v;
        v = t;
      }
    }

    return u;
  }
}
