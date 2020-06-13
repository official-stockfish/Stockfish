<%inherit file="base.mak"/>

<style>
  input[type=number].no-arrows::-webkit-inner-spin-button,
  input[type=number].no-arrows::-webkit-outer-spin-button {
      -webkit-appearance: none;
      -moz-appearance: none;
      appearance: none;
      margin: 0;
  }

  input[type=number] {
    -moz-appearance: textfield;
  }

  .flex-row {
    display: flex;
    align-items: center;
    margin: 10px 0;
  }

  .field-label {
    font-size: 12px;
    margin: 0;
    text-align: right;
    padding-right: 15px;
    width: 100px;
  }

  .field-label.leftmost {
    width: 75px;
    flex-shrink: 0;
  }

  .rightmost {
    margin-left: auto;
  }

  .third-size {
    width: 107px;
    flex-shrink: 0;
  }

  input.quarter-size {
    margin-right: 10px;
    width: 70px;
    flex-shrink: 0;
  }

  #create-new-test {
    width: 700px;
    margin: 7px auto;
    padding-right: 30px;
  }

  #create-new-test input, #create-new-test select {
    margin: 0;
  }

  .quarter-size {
    width: 80px;
    flex-shrink: 0;
  }

  .choose-test-type .btn {
    width: 75px;
  }

  #create-new-test label:hover {
    cursor: text;
  }

  #create-new-test textarea {
    width: 100%;
    min-height: 40px;
    margin: 0;
  }

  section.test-settings input {
    width: 235px;
  }
</style>

<header style="text-align: center; padding-top: 7px">
  <legend>Create New Test</legend>

  <section class="instructions" style="margin-bottom: 35px">
    Please read the
    <a href="https://github.com/glinscott/fishtest/wiki/Creating-my-first-test">Testing Guidelines</a>
    before creating your test.
  </section>
</header>

<form id="create-new-test" action="${request.url}" method="POST">
  <section class="test-settings" style="margin-bottom: 35px">
    <div class="flex-row">
      <label class="field-label leftmost">Test type</label>
      <div class="btn-group choose-test-type">
        <div class="btn" id="fast_test"
             data-options='{"tc": "10+0.1", "threads": 1, "options": "Hash=16", "bounds": "standard STC"}'>
          short (STC)
        </div>
        <div class="btn" id="slow_test"
             data-options='{"tc": "60+0.6", "threads": 1, "options": "Hash=64", "bounds": "standard LTC"}'>
          long (LTC)
        </div>
        <div class="btn" id="fast_smp_test"
             data-options='{"tc": "5+0.05", "threads": 8, "options": "Hash=64", "bounds": "standard STC"}'>
          SMP (STC)
        </div>
        <div class="btn" id="slow_smp_test"
             data-options='{"tc": "20+0.2", "threads": 8, "options": "Hash=256", "bounds": "standard LTC"}'>
          SMP (LTC)
        </div>
      </div>
      <button type="submit" class="btn btn-primary rightmost" id="submit-test"
              style="width: 180px">
        Submit test
      </button>
    </div>

    <div class="flex-row">
      <label class="field-label leftmost">Test branch</label>
      <input type="text" name="test-branch" id="test-branch"
             value="${args.get('new_tag', '')}" ${'readonly' if is_rerun else ''}>

      <label class="field-label">Base branch</label>
      <input type="text" name="base-branch" id="base-branch"
             value="${args.get('base_tag', 'master')}" ${'readonly' if is_rerun else ''}>
    </div>

    <div class="flex-row">
      <label class="field-label leftmost">Test signature</label>
      <input type="number" name="test-signature" class="no-arrows" id="test-signature"
             placeholder="Defaults to last commit message"
             value="${args.get('new_signature', '')}" ${'readonly' if is_rerun else ''}>

      <label class="field-label">Base signature</label>
      <input type="number" name="base-signature" class="no-arrows" id="base-signature"
             value="${args.get('base_signature', bench)}" ${'readonly' if is_rerun else ''}>
    </div>

    <div class="flex-row">
      <label class="field-label leftmost">Test options</label>
      <input type="text" name="new-options"
             value="${args.get('new_options', 'Hash=16')}">

      <label class="field-label">Base options</label>
      <input type="text" name="base-options"
             value="${args.get('base_options', 'Hash=16')}">
    </div>

    <div class="flex-row">
      <label class="field-label leftmost">Notes</label>
      <textarea name="run-info" placeholder="Defaults to commit message"
                rows="3">${args.get('info', '')}</textarea>
    </div>

    <div class="flex-row">
      <label class="field-label leftmost">Test repo</label>
      <input type="text" name="tests-repo" style="width: 100%;"
             value="${args.get('tests_repo', tests_repo)}" ${'readonly' if is_rerun else ''}>
    </div>
  </section>

  <section id="stop-rule" style="min-height: 130px">
    <div class="flex-row">
      <label class="field-label leftmost">Stop rule</label>
      <div class="btn-group">
        <div class="btn btn-info" data-stop-rule="sprt" style="width: 94px">SPRT</div>
        <div class="btn" data-stop-rule="numgames" style="width: 100px">Num games</div>
        <div class="btn" data-stop-rule="spsa" style="width: 94px">SPSA</div>
      </div>
      <input type="hidden" name="stop_rule" id="stop_rule_field" value="sprt" />

      <div class="rightmost" style="display: flex; align-items: center">
        <label class="field-label stop_rule spsa numgames"
               style="${'display: none' if (args.get('sprt') or not is_rerun) else ''}"># games</label>
        <input type="number" name="num-games" min="10000" step="10000"
               class="stop_rule spsa numgames third-size no-arrows"
               value="${args.get('num_games', 60000)}"
               style="${'display: none' if (args.get('sprt') or not is_rerun) else ''}" />
      </div>
    </div>

    <div class="flex-row stop_rule sprt">
      <label class="field-label leftmost stop_rule sprt">SPRT bounds</label>
      <select name="bounds" class="stop_rule sprt" style="width: 246px">
        <option value="standard STC">Standard STC {-0.5, 1.5}</option>
        <option value="standard LTC">Standard LTC {0.25, 1.75}</option>
        <option value="regression">Non-regression {-1.5, 0.5}</option>
        <option value="simplification">Simplification {-1.5, 0.5}</option>
        <option value="custom" ${is_rerun and 'selected'}>Custom bounds...</option>
      </select>

      <label class="field-label sprt custom_bounds"
             style="${args.get('sprt') or 'display: none'}">SPRT Elo0</label>
      <input type="number" step="0.05" name="sprt_elo0"
             class="sprt custom_bounds no-arrows"
             value="${args.get('sprt', {'elo0': -0.5})['elo0']}"
             style="width: 90px; ${args.get('sprt') or 'display: none'}" />

      <label class="field-label sprt custom_bounds rightmost"
             style="${args.get('sprt') or 'display: none'}">SPRT Elo1</label>
      <input type="number" step="0.05" name="sprt_elo1"
             class="sprt custom_bounds no-arrows"
             value="${args.get('sprt', {'elo1': 1.5})['elo1']}"
             style="width: 90px; ${args.get('sprt') or 'display: none'}" />
    </div>

    <div class="flex-row stop_rule spsa"
         style="${args.get('spsa') or 'display: none'}">
      <label class="field-label leftmost">SPSA A</label>
      <input type="number" min="0" step="500" name="spsa_A"
             class="third-size no-arrows"
             value="${args.get('spsa', {'A': '3000'})['A']}" />

      <label class="field-label rightmost">SPSA Alpha</label>
      <input type="number" min="0" step="0.001" name="spsa_alpha"
             class="third-size no-arrows"
             value="${args.get('spsa', {'alpha': '0.602'})['alpha']}" />

      <label class="field-label" style="margin-left: 7px">SPSA Gamma</label>
      <input type="number" min="0" step="0.001" name="spsa_gamma"
             class="third-size no-arrows"
             value="${args.get('spsa', {'gamma': '0.101'})['gamma']}" />
    </div>

    <div class="flex-row stop_rule spsa"
         style="${args.get('spsa') or 'display: none'}">
      <label class="field-label leftmost">SPSA parameters</label>
      <textarea name="spsa_raw_params"
                rows="2">${args.get('spsa', {'raw_params': ''})['raw_params']}</textarea>
    </div>
    <input type="hidden" name="spsa_clipping" value="old" />
    <input type="hidden" name="spsa_rounding" value="deterministic" />
  </section>

  <section id="worker-and-queue-options">
    <div class="flex-row">
      <label class="field-label leftmost">Threads</label>
      <input type="number" min="1" name="threads"
             class="quarter-size no-arrows"
             value="${args.get('threads', 1)}" />

      <label class="field-label" style="width: 70px">TC</label>
      <input type="text" name="tc" class="quarter-size"
             value="${args.get('tc', '10+0.1')}" />

      <label class="field-label">Priority</label>
      <input type="number" name="priority" class="quarter-size no-arrows"
             value="${args.get('priority', 0)}" />

      <label class="field-label">Throughput</label>
      <select name="throughput" class="quarter-size">
        <option value="10">10%</option>
        <option value="25">25%</option>
        <option value="50">50%</option>
        <option selected="selected" value="100">100%</option>
        <option style="color:red" value="200">200%</option>
      </select>
    </div>

    <div class="flex-row">
      <label class="field-label leftmost">Book</label>
      <input type="text" name="book" id="book"
             style="width: 229px"
             value="${args.get('book', 'noob_3moves.epd')}" />

      <label class="field-label book-depth"
             style="width: 87px; display: none">Book depth</label>
      <input type="number" min="1" name="book-depth"
             class="quarter-size no-arrows book-depth"
             style="display: none"
             value="${args.get('book_depth', 8)}" />
    </div>

    <div class="flex-row">
      <label class="field-label leftmost">Advanced</label>
      <input type="checkbox" name="auto-purge" checked="checked"
             value="True" />
      <span style="margin-left: 10px">Auto-purge</span>
    </div>
  </section>

  %if 'resolved_base' in args:
    <input type="hidden" name="resolved_base" value="${args['resolved_base']}">
    <input type="hidden" name="resolved_new" value="${args['resolved_new']}">
    <input type="hidden" name="msg_base" value="${args.get('msg_base', '')}">
    <input type="hidden" name="msg_new" value="${args.get('msg_new', '')}">
  %endif

  %if is_rerun:
    <input type="hidden" name="rescheduled_from" value="${rescheduled_from}">
  %endif
</form>

<script type="text/javascript">
  $(window).bind('pageshow', function() {
    // If pressing the 'back' button to get back to this page, make sure
    // the submit test button is enabled again.
    $('#submit-test').removeAttr('disabled').text('Submit test');
  });

  const preset_bounds = {
    'standard STC': [-0.5, 1.5],
    'standard LTC': [0.25, 1.75],
    'regression': [-1.5, 0.5],
    'simplification': [-1.5, 0.5],
  };

  function update_sprt_bounds(selected_bounds_name) {
    if (selected_bounds_name === 'custom') {
      $('.custom_bounds').show();
    } else {
      $('.custom_bounds').hide();
      const bounds = preset_bounds[selected_bounds_name];
      $('input[name=sprt_elo0]').val(bounds[0]);
      $('input[name=sprt_elo1]').val(bounds[1]);
    }
  }

  function update_book_depth_visibility(book) {
    if (book.match('\.pgn$')) {
      $('.book-depth').show();
    } else {
      $('.book-depth').hide();
    }
  }

  $('select[name=bounds]').on('change', function() {
    update_sprt_bounds($(this).val());
  });

  var initial_base_branch = $('#base-branch').val();
  var initial_base_signature = $('#base-signature').val();
  var spsa_do_not_save = false;
  $('.btn-group .btn').on('click', function() {
    if(!spsa_do_not_save){
      initial_base_branch = $('#base-branch').val();
      initial_base_signature = $('#base-signature').val();
    }
    const $btn = $(this);
    $(this).parent().find('.btn').removeClass('btn-info');
    $(this).addClass('btn-info');

    // choose test type - STC, LTC - sets preset values
    const test_options = $btn.data('options');
    if (test_options) {
      const { tc, threads, options, bounds } = test_options;
      if (test_options) {
        $('input[name=tc]').val(tc);
        $('input[name=threads]').val(threads);
        $('input[name=new-options]').val(options);
        $('input[name=base-options]').val(options);
        $('select[name=bounds]').val(bounds);
        update_sprt_bounds(bounds);
      }
    }

    // stop-rule buttons - SPRT, Num games, SPSA - toggles stop-rule fields
    const stop_rule = $btn.data('stop-rule');
    if (stop_rule) {
      $('.stop_rule').hide();
      $('#stop_rule_field').val(stop_rule);
      $('.' + stop_rule).show();
      %if not is_rerun:
        if (stop_rule === 'spsa') {
          // base branch and test branch should be the same for SPSA tests
          $('#base-branch').attr('readonly', 'true').val($('#test-branch').val());
          $('#test-branch').on('input', function() {
            $('#base-branch').val($(this).val());
          })
          $('#base-signature').attr('readonly', 'true').val($('#test-signature').val());
          $('#test-signature').on('input', function() {
            $('#base-signature').val($(this).val());
          })
	  spsa_do_not_save = true;
        } else {
          $('#base-branch').removeAttr('readonly').val(initial_base_branch);
          $('#base-signature').removeAttr('readonly').val(initial_base_signature);
          $('#test-branch').off('input');
          $('#test-signature').off('input');
	  spsa_do_not_save = false;
        }
      %endif
      if (stop_rule === 'sprt') {
        update_sprt_bounds($('select[name=bounds]').val());
      }
    }
  });

  // Only .pgn book types have a book_depth field
  update_book_depth_visibility($("#book").val());
  $('#book').on('input', function() {
    update_book_depth_visibility($(this).val());
  });

  let form_submitted = false;
  $('#create-new-test').on('submit', function(event) {
    if (form_submitted) {
      // Don't allow submitting the form more than once
      $(event).preventDefault();
      return;
    }
    form_submitted = true;
    $('#submit-test').attr('disabled', true).text('Submitting test...');
  });

  const is_rerun = ${'true' if is_rerun else 'false'};
  if (is_rerun) {
    // Select the correct fields by default for re-runs
    const tc = '${args.get('tc')}';
    if (tc === '10+0.1') {
      $('.choose-test-type .btn').removeClass('btn-info');
      $('.btn#fast_test').addClass('btn-info');
    } else if (tc === '60+0.6') {
      $('.choose-test-type .btn').removeClass('btn-info');
      $('.btn#slow_test').addClass('btn-info');
    } else if (tc === '5+0.05') {
      $('.choose-test-type .btn').removeClass('btn-info');
      $('.btn#fast_smp_test').addClass('btn-info');
    } else if (tc === '20+0.2') {
      $('.choose-test-type .btn').removeClass('btn-info');
      $('.btn#slow_smp_test').addClass('btn-info');
    }
    %if args.get('spsa'):
      $('.btn[data-stop-rule="spsa"]').trigger('click');
    %elif not args.get('sprt'):
      $('.btn[data-stop-rule="numgames"]').trigger('click');
    %endif
  } else {
    // short STC test by default for new tests
    $('.btn#fast_test').addClass('btn-info');
    // Focus the "Test branch" field on page load for new tests
    $('#test-branch').focus();
  }
</script>
