library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity add is
  port(
    a : in signed(31 downto 0);
    b : in signed(31 downto 0);
    y : out signed(31 downto 0)
  );
end entity;
architecture rtl of add is
begin
  process(all) is
    variable s : signed(31 downto 0);
  begin
    y <= (others => '0');
    s := (a + b);
    y <= s;
  end process;
end architecture;

