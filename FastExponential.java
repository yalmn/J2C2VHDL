// vhdl-dialect: v93
// entity-name: fe
// output-name: out
// sensitivity-list: base, exponent, modulus
public class FastExponential {
  public static int fastExponential(int base, int exponent, int modulus) {
    int b;
    int e;
    int res;
    int i;

    if ((modulus <= 0) || (modulus == 1) || (exponent < 0)) {
      return 0;
    } else {
      b = base % modulus;
      if (b < 0) {
        b = b + modulus;
      }
      e = exponent;
      res = 1 % modulus;

      for (i = 0; i < 31; i = i + 1) {
        if ((e % 2) == 1) {
          res = (res * b) % modulus;
        }
        b = (b * b) % modulus;
        e = e / 2;
      }

      return res;
    }
  }
}
