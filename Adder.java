// vhdl-dialect: v93
// entity-name: add_v93
// output-name: sum_out
// sensitivity-list: a, b
public class Adder {
  public static int add(int a, int b) {
    int s = a + b; 
    if (s > 100) {
      return 100;
    }
    return s;
  }
}