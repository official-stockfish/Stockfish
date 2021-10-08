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

import androidx.appcompat.app.AppCompatActivity;

import android.content.Context;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.KeyEvent;
import android.view.inputmethod.InputMethodManager;
import android.widget.TextView;

import com.ab.ndktest.databinding.ActivityMainBinding;
import com.ab.pgn.stockfish.Stockfish;

/**
 * this is a minimalistic example of using Stockfish as a shared library on Android
 */
public class MainActivity extends AppCompatActivity {
    enum READ_COMMAND {
        READ_NORMAL,
        READ_ERR,
    }
    private final Stockfish stockfish = new Stockfish();
    private String fromSF;
    private TextView tvResponse;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ActivityMainBinding binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        tvResponse = binding.textViewResponse;
        TextView tvCommand = binding.editTextCommand;
        tvCommand.setOnEditorActionListener(new TextView.OnEditorActionListener() {
            @Override
            public boolean onEditorAction(TextView textView, int actionId, KeyEvent keyEvent) {
                /* Hide Soft Keyboard IN ONE LINE */
                ((InputMethodManager)MainActivity.this.getSystemService(Context.INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(textView.getWindowToken(),0);
                String cmd = tvCommand.getText().toString().trim();
                if (!"quit".equals(cmd)) {
                    stockfish.write(cmd);
                }
                return false;
            }
        });

        bgCall(() -> sfRead(READ_COMMAND.READ_NORMAL));
        bgCall(() -> sfRead(READ_COMMAND.READ_ERR));
    }

    private void sfRead(READ_COMMAND read_command) {
        while ((fromSF = read_command == READ_COMMAND.READ_ERR ? stockfish.read_err() : stockfish.read()) != null) {
            if (fromSF.trim().isEmpty()) {
                continue;
            }
            // tvResponse cannot be modified directly in a BG thread
            new Handler(Looper.getMainLooper()).post(() -> {
                int color = read_command == READ_COMMAND.READ_ERR ?
                        Color.parseColor("#ff0000") :
                        Color.parseColor("#000000");
                tvResponse.setTextColor(color);
                tvResponse.setText(fromSF);
            });
        }
    }

    private void bgCall(BgCall caller) {
        new Thread(caller::exec).start();
    }

    private interface BgCall {
        void exec();
    }
}