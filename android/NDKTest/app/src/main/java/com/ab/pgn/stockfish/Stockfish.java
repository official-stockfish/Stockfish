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
package com.ab.pgn.stockfish;

public class Stockfish {
    public static final String DEFAULT_SF_PATH = "stockfish";

    public native void _init();
    public native void _write(String command);
    public native String _read();
    public native String _read_err();

    public Stockfish() {
        init(DEFAULT_SF_PATH);
    }

    public Stockfish(String path) {
        init(path);
    }

    private void init(String path) {
        System.loadLibrary(path);
        _init();
    }

    public void write(String command) {
        _write(command);
    }
    public String read() {
        return _read();
    }
    public String read_err() {
        return _read_err();
    }
    public void quit() {
        _write("quit");
    }


}
