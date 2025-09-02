public class Adder {
  public static int add(int a, int b) {
    int s = a + b; // sum
    if (s > 100) {
      return 100;
    }
    return s;
  }
}