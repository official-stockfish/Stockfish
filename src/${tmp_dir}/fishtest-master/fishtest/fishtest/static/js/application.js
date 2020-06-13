const getCellValue = (tr, idx) => tr.children[idx].innerText || tr.children[idx].textContent;

const comparer = (idx, asc) => (a, b) => ((v1, v2) =>
    v1 !== '' && v2 !== '' && !isNaN(v1) && !isNaN(v2) ? v1 - v2 : v1.toString().localeCompare(v2)
)(getCellValue(asc ? a : b, idx), getCellValue(asc ? b : a, idx));

$(() => {
    // clicking on table headers sorts the rows
    $(document).on('click', 'th', (event) => {
        const th = $(event.currentTarget)[0];
        const table = th.closest('table');
        const body = table.querySelector('tbody');
        Array.from(body.querySelectorAll('tr'))
            .sort(comparer(Array.from(th.parentNode.children).indexOf(th), this.asc = !this.asc))
            .forEach(tr => body.appendChild(tr));
    });

    // clicking Show/Hide on Pending tests and Machines sections toggles them
    $("#pending-button").click(function () {
        var active = $(this).text().trim() === 'Hide';
        $(this).text(active ? 'Show' : 'Hide');
        $("#pending").slideToggle(150);
        $.cookie('pending_state', $(this).text().trim());
    });

    let fetchingMachines = false;
    $("#machines-button").click(function () {
        const active = $(this).text().trim() === 'Hide';
        $(this).text(active ? 'Show' : 'Hide');
        if (!active && !$("#machines table")[0] && !fetchingMachines) {
            fetchingMachines = true;
            $.get("/tests/machines", (html) => $("#machines").append(html));
        }
        $("#machines").toggle();
        $.cookie('machines_state', $(this).text().trim());
    });

    // CSRF protection for links and forms
    const csrfToken = $("meta[name='csrf-token']").attr('content');
    $("#logout").on("click", (event) => {
        event.preventDefault();
        $.ajax({
            url: "/logout",
            method: "POST",
            headers: {
                'X-CSRF-Token': csrfToken
            },
            success: () => {
                window.location = "/";
            }
        });
    });
    $("form[method='POST']").each((i, $form) => {
        $("<input>")
            .attr("type", "hidden")
            .attr("name", "csrf_token").val(csrfToken)
            .appendTo($form);
    });
});
