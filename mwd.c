#include "mwd.h"

static void setSelection(struct wl_listener *listener, void *data)
{
	mwdServer										*server	= wl_container_of(listener, server, setSelection);
	struct wlr_seat_request_set_selection_event		*event	= data;

	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

int main(int argc, char **argv)
{
	mwdServer			server;
	char				*rcfile = "./mwdrc"; // TODO Set a better default value; ie $XDG_CONFIG_HOME/mwd/mwdrc
	int					c;
	const char			*socket;

	memset(&server, 0, sizeof(server));

	// TODO Let the user call this again to change the verbosity
	wlr_log_init(WLR_DEBUG, NULL);

	while (-1 != (c = getopt(argc, argv, "s:h"))) {
		switch (c) {
			case 's':
				rcfile = optarg;
				break;

			default:
				printf("Usage: %s [-s startup command]\n", argv[0]);
				return 0;
		}
	}

	if (optind < argc) {
		printf("Usage: %s [-s startup command]\n", argv[0]);
		return 0;
	}

	/* Create the wayland display */
	server.display = wl_display_create();

	/*
		Create a wlroots backend

		The backend abstracts the input and output hardware. Using autocreate
		will pick the most suitable backend for us (X11 window vs TTY)
	*/
	server.backend = wlr_backend_autocreate(server.display, NULL);
	server.renderer = wlr_backend_get_renderer(server.backend);
	wlr_renderer_init_wl_display(server.renderer, server.display);

	wlr_compositor_create(server.display, server.renderer);
	wlr_data_device_manager_create(server.display);

	server.layout = wlr_output_layout_create();

	server.layoutChanged.notify = OutputLayoutChanged;
	wl_signal_add(&server.layout->events.change, &server.layoutChanged);
	server.output.applying = false;

	/*
		Setup our list of views and shells:
			https://drewdevault.com/2018/07/29/Wayland-shells.html
	*/
	wl_list_init(&server.views.drawOrder);
	wl_list_init(&server.views.userOrder);

	/*
		xdg shell

		This is responsible for all "normal" application windows.
	*/
	XdgMain(&server);

	/*
		layer shell

		This is responsible for desktop environment stuff. Background, bar,
		lock screen, etc.
	*/
	LayerMain(&server);

	/*
		Setup the xdg output manager

		This will automatically send extra information about connected outputs
		to clients.
	*/
    wlr_xdg_output_manager_v1_create(server.display, server.layout);

	/* Register for notifications when there is a new output */
	wl_list_init(&server.outputs);
	server.output.added.notify = OutputAdd;
	wl_signal_add(&server.backend->events.new_output, &server.output.added);

	/*
		Setup the wlr output manager

		This implements the wlr-output-management protocol, which allows clients
		to configure and test output display options (ie resolution, refresh
		rate, scale, etc)
	*/
	server.output.mgr = wlr_output_manager_v1_create(server.display);

	server.output.apply.notify = OutputApplyCfg;
	wl_signal_add(&server.output.mgr->events.apply, &server.output.apply);

	server.output.test.notify = OutputTestCfg;
	wl_signal_add(&server.output.mgr->events.test, &server.output.test);

	// TODO Decoration manager
	// TODO Idle
	// TODO relative_pointer_manager
	// TODO pointer_constraints
	// TODO output_power_manager_v1
	// TODO dmabuf_manager
	// TODO screencopy_manager
	// TODO all the other managers

#if 0
	// TODO Create the wlr_xwayland shell
	server.xwayland.wlr_xwayland = wlr_xwayland_create(server->display, server->compositor, TRUE);
	if (!server->xwayland.wlr_xwayland) {
		sway_log(SWAY_ERROR, "Failed to start Xwayland");
		unsetenv("DISPLAY");
	} else {
		server->xwayland_surface.notify = handle_xwayland_surface;
		wl_signal_add(&server->xwayland.wlr_xwayland->events.new_surface, &server->xwayland_surface);

		server->xwayland_ready.notify = handle_xwayland_ready;
		wl_signal_add(&server->xwayland.wlr_xwayland->events.ready, &server->xwayland_ready);

		setenv("DISPLAY", server->xwayland.wlr_xwayland->display_name, true);
	}
#else
	unsetenv("DISPLAY");
#endif

	inputMain(&server);

	server.setSelection.notify = setSelection;
	wl_signal_add(&server.seat->events.request_set_selection, &server.setSelection);


	/* Add a Unix socket to the Wayland display. */
	if (!(socket = wl_display_add_socket_auto(server.display))) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	/* Start the backend */
	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}

	/*
		Set the WAYLAND_DISPLAY environment variable to point to our socket and
		then run the configured rc file.
	*/
	setenv("WAYLAND_DISPLAY", socket, true);

	if (rcfile && fork() == 0) {
		/* Child */
		execl("/bin/sh", "/bin/sh", "-c", rcfile, (void *)NULL);
		return 1;
	}

	/*
		Run the Wayland event loop
	*/
	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
	wl_display_run(server.display);

	/* Cleanup */
	wl_display_destroy_clients(server.display);
	wl_display_destroy(server.display);
	return 0;
}

