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

static void OutputApply(mwdServer *server, struct wlr_output_configuration_v1 *config, bool live)
{
	struct wlr_output_configuration_head_v1		*head;
	struct wlr_output_head_v1_state				*state;
	struct wlr_output_mode						*mode;
	mwdOutput									*output;
	bool										valid	= true;

	wl_list_for_each(head, &config->heads, link) {
		// TODO Add some debug output

		state = &head->state;
		if (!(output = OutputFind(server, state->output))) {
			// TODO Uh, we don't know about this output...
			continue;
		}

		wlr_output_enable(output->output, state->enabled);
		if (!state->enabled) {
			continue;
		}

		if (state->custom_mode.width > 0 && state->custom_mode.height > 0) {
			wlr_output_set_custom_mode(output->output, state->custom_mode.width, state->custom_mode.height, state->custom_mode.refresh);
		} else if ((mode = state->mode) || (mode = wlr_output_preferred_mode(output->output))) {
			wlr_output_set_mode(output->output, mode);
		}

		// TODO Add support for other options
		//		- transform
		//		- scale
		//		- adaptive_sync
		//		- subpixel layout
		//		- description

		valid &= wlr_output_test(output->output);
	}

	wl_list_for_each(output, &server->outputs, link) {
		if (valid && live) {
			valid &= wlr_output_commit(output->output);
		}

		if (!valid || !live) {
			wlr_output_rollback(output->output);
		}
	}

	if (valid) {
		wlr_output_configuration_v1_send_succeeded(config);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);
}

void OutputApplyCfg(struct wl_listener *listener, void *data)
{
	mwdServer							*server		= wl_container_of(listener, server, output.apply);
	struct wlr_output_configuration_v1	*config = data;

	OutputApply(server, config, true);
}

void OutputTestCfg(struct wl_listener *listener, void *data)
{
	mwdServer							*server		= wl_container_of(listener, server, output.test);
	struct wlr_output_configuration_v1	*config = data;

	OutputApply(server, config, false);
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
		// TODO Let the user configure their desired mode for this output
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

	// TODO Let the user configure the layout for this output
	wlr_output_layout_add_auto(server->layout, wlr_output);
}


