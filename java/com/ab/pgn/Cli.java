/*
     Copyright (C) 2021	Alexander Bootman, alexbootman@gmail.com

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

export JAVA_HOME=/usr/lib/jvm/java-8-oracle (or something like this)
  navigate to java directory
javac com/ab/pgn/Cli.java com/ab/pgn/StockfishRunner.java com/ab/pgn/stockfish/Stockfish.java
java -cp . com.ab.pgn.Cli ../src/stockfish.so
*/
package com.ab.pgn;

import com.ab.pgn.stockfish.Stockfish;
import java.io.*;

public class Cli {
    public static final String SF_LIB_NAME = "stockfish.so";
    public static void main(String[] args) throws Exception {
        String libPath = null;
        if (args.length >= 1) {
            libPath = args[0];
        } else {
            libPath = "x";
        }
        File f = new File(libPath);
        libPath = f.getAbsolutePath();
        int i = libPath.lastIndexOf(File.separator);
        libPath = libPath.substring(0, i + 1) + SF_LIB_NAME;
        StockfishRunner stockfishRunner = new StockfishRunner(libPath);
        stockfishRunner.run();
    }
}
