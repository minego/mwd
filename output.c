#include "../mwd.h"

struct renderData
{
	struct wlr_output		*output;
	struct wlr_renderer		*renderer;
	struct timespec			*when;
	mwdView					*view;
};

static void renderSurface(struct wlr_surface *surface, int sx, int sy, void *data)
{
	/* This function is called for every view that needs to be rendered. */
	struct renderData			*rdata	= data;
	mwdView						*view	= rdata->view;
	struct wlr_output			*output	= rdata->output;
	struct wlr_texture			*texture;
	struct wlr_box				box;
	double						ox		= 0;
	double						oy		= 0;
	double						width, height;
	float						matrix[9];
	enum wl_output_transform	transform;

	if (!(texture = wlr_surface_get_texture(surface))) {
		return;
	}

	/* Calculate the coordinates for this view relative to the output */
	wlr_output_layout_output_coords(view->server->layout, output, &ox, &oy);
	ox += sx;
	oy += sy;

	if (surface == ViewGetSurface(view)) {
		ViewGetSize(view, &width, &height);
	} else {
		width = surface->current.width;
		width = surface->current.height;
	}

	if (view->edges & WLR_EDGE_TOP) {
		oy += view->top;
		view->bottom = view->top + height;
	} else {
		oy += view->bottom - height;
		view->top = view->bottom - height;
	}

	if (view->edges & WLR_EDGE_LEFT) {
		ox += view->left;
		view->right = view->left + width;
	} else {
		ox += view->right - width;
		view->left = view->right - width;
	}

	/*
		Apply the scale for this output. This is not enough to fully support
		HiDPI.
	*/
	box.x		= output->scale * ox;
	box.y		= output->scale * oy;
	box.width	= output->scale * surface->current.width;
	box.height	= output->scale * surface->current.height;

	transform	= wlr_output_transform_invert(surface->current.transform);
	wlr_matrix_project_box(matrix, &box, transform, 0, output->transform_matrix);

	/* Perform the actual render on the GPU */
	wlr_render_texture_with_matrix(rdata->renderer, texture, matrix, 1);

	/* Let the client know that we've displayed that frame */
	wlr_surface_send_frame_done(surface, rdata->when);
}

static void renderFrame(struct wl_listener *listener, void *data)
{
	mwdOutput				*output		= wl_container_of(listener, output, frame);
	struct wlr_renderer		*renderer	= output->server->renderer;
	mwdView					*view;
	struct timespec			now;
	int						width, height;
	float					color[4]	= {0.3, 0.3, 0.3, 1.0};
	struct renderData		rdata;
	mwdLayer				layer;

	clock_gettime(CLOCK_MONOTONIC, &now);

	/* wlr_output_attach_render makes the OpenGL context current. */
	if (!wlr_output_attach_render(output->output, NULL)) {
		return;
	}

	wlr_output_effective_resolution(output->output, &width, &height);

	/* Begin the renderer (calls glViewport and some other GL sanity checks) */
	wlr_renderer_begin(renderer, width, height);

	// TODO Let a user configure this color
	wlr_renderer_clear(renderer, color);

	for (layer = MWD_LAYER_BEFORE + 1; layer < MWD_LAYER_AFTER; layer++) {
		wl_list_for_each_reverse(view, &output->server->views.drawOrder, link.drawOrder) {
			if (view->renderLayer != layer) {
				continue;
			}

			if (!ViewIsVisible(view, output)) {
				continue;
			}
			rdata.output	= output->output;
			rdata.view		= view;
			rdata.renderer	= renderer;
			rdata.when		= &now;

			ViewForEachSurface(view, renderSurface, &rdata);
		}
	}

	wlr_output_render_software_cursors(output->output, NULL);

	/* Conclude rendering, swap the buffers, show the final frame on screen */
	wlr_renderer_end(renderer);
	wlr_output_commit(output->output);
}

/* Return the first item if called with a NULL */
mwdOutput *OutputFind(mwdServer *server, struct wlr_output *output)
{
	mwdOutput		*o;

	wl_list_for_each(o, &server->outputs, link) {
		if (o && (o->output == output || output == NULL)) {
			return o;
		}
	}
	return NULL;
}

#define TEST_TIMEOUT_SECS 15
static int OutputConfigTestTimeout(void *data)
{
	OutputTestRevert((mwdOutputTest *) data);
	return 0;
}

/* Commit the provided configuration to the active outputs */
void OutputConfigApply(mwdServer *server, struct wlr_output_configuration_v1 *config)
{
	struct wlr_output_configuration_head_v1		*head;
	struct wlr_output							*o;

	/* Prevent sending events to the clients until we are done */
	server->output.applying = true;

	wl_list_for_each(head, &config->heads, link) {
		o = head->state.output;

		if (head->state.enabled && !o->enabled) {
			wlr_output_layout_add_auto(server->layout, o);
		} else if (!head->state.enabled && o->enabled) {
			wlr_output_layout_remove(server->layout, o);
		}

		wlr_output_enable(o, head->state.enabled);

		/* All other settings only have an effect if the output is enabled. */
		if (head->state.enabled) {
			if (head->state.mode) {
				wlr_output_set_mode(o, head->state.mode);
			} else {
				wlr_output_set_custom_mode(o,
						head->state.custom_mode.width, head->state.custom_mode.height,
						head->state.custom_mode.refresh);
			}

			wlr_output_layout_move(server->layout, o, head->state.x, head->state.y);

			wlr_output_set_scale(o, head->state.scale);
			wlr_output_set_transform(o, head->state.transform);
		}
		wlr_output_commit(o);
	}

	/* Allow output change events to resume */
	server->output.applying = false;
}

/*
	Create a configuration object that represents the current active
	configuration of all outputs, suitable for sending to clients.
*/
static struct wlr_output_configuration_v1 *OutputConfigCreate(mwdServer *server)
{
	struct wlr_output_configuration_v1		*config;
	struct wlr_output_configuration_head_v1	*head;
	struct wlr_box							*box;
	mwdOutput								*output;

	if (!(config = wlr_output_configuration_v1_create())) {
		return NULL;
	}

	wl_list_for_each(output, &server->outputs, link) {
		if (!(head = wlr_output_configuration_head_v1_create(config, output->output))) {
			wlr_output_configuration_v1_destroy(config);
			return NULL;
		}

		if ((box = wlr_output_layout_get_box(server->layout, output->output))) {
			head->state.x = box->x;
			head->state.y = box->y;
		}
	}
	return config;
}

static void OutputTestFree(mwdOutputTest *test)
{
	if (test) {
		if (test->server && test->server->output.pendingTest == test) {
			test->server->output.pendingTest = NULL;
		}

		if (test->timer) {
			wl_event_source_remove(test->timer);
		}

		if (test->newConfig) {
			wlr_output_configuration_v1_destroy(test->newConfig);
		}

		if (test->oldConfig) {
			wlr_output_configuration_v1_destroy(test->oldConfig);
		}

		free(test);
	}
}

void OutputTestApply(mwdOutputTest *test)
{
	OutputTestFree(test);
}

void OutputTestRevert(mwdOutputTest *test)
{
	OutputConfigApply(test->server, test->oldConfig);
	wlr_output_configuration_v1_send_failed(test->newConfig);

	OutputTestFree(test);
}

void OutputLayoutChanged(struct wl_listener *listener, void *data)
{
	mwdServer							*server = wl_container_of(listener, server, layoutChanged);
	struct wlr_output_configuration_v1	*config;

	if (server->output.applying) {
		/* A change event for all the pending changes will be sent when they are complete */
		return;
	}

	/* Give all connected clients a new configuration object */
	if ((config = OutputConfigCreate(server))) {
		/* the set_configuration() call will destroy the configuration object for us */
		wlr_output_manager_v1_set_configuration(server->output.mgr, config);
	}
}

/* A client has requested that a permanent change be applied to the output configuration */
void OutputApplyCfg(struct wl_listener *listener, void *data)
{
	mwdServer							*server		= wl_container_of(listener, server, output.apply);
	struct wlr_output_configuration_v1	*config		= data;

	OutputConfigApply(server, config);

	wlr_output_configuration_v1_send_succeeded(config);
	wlr_output_configuration_v1_destroy(config);
}

/* A client has requested that a change be tested by applying it to the output configuration */
void OutputTestCfg(struct wl_listener *listener, void *data)
{
	mwdServer							*server		= wl_container_of(listener, server, output.test);
	struct wlr_output_configuration_v1	*config		= data;
	mwdOutputTest						*test;
	struct wl_event_loop				*loop;

	/* If a test is already active then reject that test */

	/* If a test is already active, immediately tell the client that the request
	 * failed and that the test configuration should be destroyed. */
	if (server->output.pendingTest) {
		wlr_output_configuration_v1_send_failed(config);
		wlr_output_configuration_v1_destroy(config);

		return;
	}

	if (!(test = calloc(1, sizeof(struct mwdOutputTest)))) {
		goto failure;
	}

	test->server		= server;
	test->newConfig		= config;
	test->oldConfig		= OutputConfigCreate(server);

	if (!test->oldConfig) {
		goto failure;
	}

	/*
		Create a timer on the server's event look to automatically revert the
		configuration if the user doesn't commit or reject it within 15 seconds.
	*/
	loop = wl_display_get_event_loop(server->display);
	if (!(test->timer = wl_event_loop_add_timer(loop, OutputConfigTestTimeout, test))) {
		goto failure;
	}

	if (wl_event_source_timer_update(test->timer, TEST_TIMEOUT_SECS * 1000)) {
		goto failure;
	}

	server->output.pendingTest = test;

	/* Now apply the new configuration so the user can see the result. */
	OutputConfigApply(server, test->newConfig);
	return;

failure:
	wlr_output_configuration_v1_send_failed(config);
	OutputTestFree(test);
}

void OutputAdd(struct wl_listener *listener, void *data)
{
	mwdServer				*server		= wl_container_of(listener, server, output.added);
	struct wlr_output		*wlr_output	= data;
	struct wlr_output_mode	*mode;
	mwdOutput				*output;

	/*
		Depending on the backend there may or may not be modes. If there are
		then attempt to select the "preferred" one for now.
	*/
	if (!wl_list_empty(&wlr_output->modes)) {
		mode = wlr_output_preferred_mode(wlr_output);

		wlr_output_set_mode(wlr_output, mode);
		wlr_output_enable(wlr_output, true);

		if (!wlr_output_commit(wlr_output)) {
			return;
		}
	}

	/* Allocates and configures our state for this output */
	if (!(output = calloc(1, sizeof(mwdOutput)))) {
		return;
	}
	output->output		= wlr_output;
	output->server		= server;

	/* Sets up a listener for the frame notify event */
	output->frame.notify = renderFrame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	wl_list_insert(&server->outputs, &output->link);

	wlr_output_layout_add_auto(server->layout, wlr_output);
}


