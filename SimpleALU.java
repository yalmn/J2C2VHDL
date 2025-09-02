// vhdl-dialect: v93
// entity-name: simplealu
// output-name: sum_out
// sensitivity-list: a, b, cmnd
public class SimpleALU {
   public static int execute(int a, int b, int cmnd) {
        if (cmnd == 1) {        
            return a + b;
        }
        if (cmnd == 2) {        
            return a - b;
        }
        if (cmnd == 3) {       
            return a * b;
        }
        return 0; 
    }
}