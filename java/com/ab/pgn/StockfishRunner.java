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
*/
package com.ab.pgn;

import com.ab.pgn.stockfish.Stockfish;
import java.util.Scanner;

public class StockfishRunner {
    private final Stockfish stockfish;

    public StockfishRunner(String path) {
        stockfish = new Stockfish(path);
    }

    void input_reader() {
        Scanner scanner = ScannerHolder.getScanner();
        System.out.print("Enter Stockfish commands:");

        while (scanner.hasNext()) {
            String cmd = scanner.next();
            stockfish.write(cmd);
            if (cmd.equals("quit")) {
                break;
            }
        }
    }

    void output_reader() {
        String fromSF;
        while((fromSF = stockfish.read()) != null) {
            System.out.print(fromSF);
        }
    }

    void err_reader() {
        String fromSF;
        while((fromSF = stockfish.read_err()) != null) {
            System.err.print(fromSF);
        }
    }

    public void run() throws InterruptedException {
        Thread inputThread = new Thread(() -> input_reader());
        inputThread.start();

        Thread outputThread = new Thread(() -> output_reader());
        outputThread.start();

        Thread errorThread = new Thread(() -> err_reader());
        errorThread.start();

        inputThread.join();
        outputThread.join();
        errorThread.join();
    }

    private static class ScannerHolder {
        private static final Scanner scanner = new Scanner(System.in);  // singleton
        private static Scanner getScanner() {
            return scanner;
        }
    }

}
