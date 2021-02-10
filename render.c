#include "../mwd.h"

#if 0
/// Render the given view's borders, on the given output. The border will be the active
/// colour if is_active is true, or otherwise the inactive colour.
static void RenderBorders(struct viv_view *view, struct viv_output *output, bool is_active) {
	struct wlr_renderer *renderer = output->server->renderer;

    struct viv_server *server = output->server;
    int gap_width = server->config->gap_width;

    double x = 0, y = 0;
	wlr_output_layout_output_coords(server->output_layout, output->wlr_output, &x, &y);
	x += view->target_x + gap_width;
    y += view->target_y + gap_width;
    int width = MAX(1, view->target_width - 2 * gap_width);
    int height = MAX(1, view->target_height - 2 * gap_width);
    float *colour = (is_active ?
                     server->config->active_border_colour :
                     server->config->inactive_border_colour);

    int line_width = server->config->border_width;

    // TODO: account for scale factor

    struct wlr_box box;

    // bottom
    box.x = x;
    box.y = y;
    box.width = width;
    box.height = line_width;
    wlr_render_rect(renderer, &box, colour, output->wlr_output->transform_matrix);

    // top
    box.x = x;
    box.y = y + height - line_width;
    box.width = width;
    box.height = line_width;
    wlr_render_rect(renderer, &box, colour, output->wlr_output->transform_matrix);

    // left
    box.x = x;
    box.y = y;
    box.width = line_width;
    box.height = height;
    wlr_render_rect(renderer, &box, colour, output->wlr_output->transform_matrix);

    // right
    box.x = x + width - line_width;
    box.y = y;
    box.width = line_width;
    box.height = height;
    wlr_render_rect(renderer, &box, colour, output->wlr_output->transform_matrix);

}
#endif

void RenderSurface(struct wlr_surface *surface, int sx, int sy, void *data)
{
	/* This function is called for every view that needs to be rendered. */
	mwdRenderData				*rdata	= data;
	mwdView						*view	= rdata->view;
	struct wlr_output			*output	= rdata->output;
	struct wlr_texture			*texture;
	double						top, right, bottom, left;
	struct wlr_box				box;
	double						ox		= 0;
	double						oy		= 0;
	double						width, height;
	float						matrix[9];
	enum wl_output_transform	transform;

	if (!(texture = wlr_surface_get_texture(surface))) {
		return;
	}

	ViewGetRenderPos(view, &top, &right, &bottom, &left);

	if (surface == ViewGetSurface(view)) {
		width = right - left;
		height = bottom - top;
	} else {
		width = surface->current.width;
		height = surface->current.height;
	}

	/* Calculate the coordinates for this view relative to the output */
	wlr_output_layout_output_coords(view->server->layout, output, &ox, &oy);

	box.height	= height;
	box.width	= width;
	box.y		= top + oy;
	box.x		= left + ox;

	box.x += (rdata->sx + sx);
	box.y += (rdata->sy + sy);

	/*
		Apply the scale for this output. This is not enough to fully support
		HiDPI.
	*/
	box.x		*= output->scale;
	box.y		*= output->scale;
	box.width	*= output->scale;
	box.height	*= output->scale;

	transform	= wlr_output_transform_invert(surface->current.transform);
	wlr_matrix_project_box(matrix, &box, transform, 0, output->transform_matrix);

	/* Perform the actual render on the GPU */
	wlr_render_texture_with_matrix(rdata->renderer, texture, matrix, 1);

	/* Let the client know that we've displayed that frame */
	wlr_surface_send_frame_done(surface, &rdata->when);
}

void RenderPopupSurface(struct wlr_surface *surface, int sx, int sy, void *data)
{
	mwdRenderData				*rdata	= data;

    rdata->sx = sx;
    rdata->sy = sy;

    wlr_surface_for_each_surface(surface, RenderSurface, rdata);
}

void RenderFrame(struct wl_listener *listener, void *data)
{
	mwdOutput				*output		= wl_container_of(listener, output, frame);
	struct wlr_renderer		*renderer	= output->server->renderer;
	mwdView					*view;
	struct timespec			now;
	int						width, height;
	float					color[4]	= {0.3, 0.3, 0.3, 1.0};
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

			RenderView(view, renderer, output);
		}
	}

	wlr_output_render_software_cursors(output->output, NULL);

	/* Conclude rendering, swap the buffers, show the final frame on screen */
	wlr_renderer_end(renderer);
	wlr_output_commit(output->output);
}

