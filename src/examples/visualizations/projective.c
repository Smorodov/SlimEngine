#include "./projective_base.h"
#include "./projective_process.h"

void onButtonDown(MouseButton *mouse_button) {
    app->controls.mouse.pos_raw_diff = Vec2i(0, 0);

    if (app->viewport.settings.show_hud && current_viz != VIEW_FRUSTUM_SLICE) {
        i32 w = secondary_viewport_frame_buffer.dimensions.width;
        i32 h = secondary_viewport_frame_buffer.dimensions.height;
        i32 x = secondary_viewport.settings.position.x;
        i32 y = secondary_viewport.settings.position.y;
        active_viewport = (
                inRange(app->controls.mouse.pos.x, w + x, x) &&
                inRange(app->controls.mouse.pos.y, h + y, y)
                ) ?
                        &secondary_viewport :
                        &app->viewport;
    }
}

void onDoubleClick(MouseButton *mouse_button) {
    if (mouse_button == &app->controls.mouse.left_button) {
        app->controls.mouse.is_captured = !app->controls.mouse.is_captured;
        app->platform.setCursorVisibility(!app->controls.mouse.is_captured);
        app->platform.setWindowCapture(    app->controls.mouse.is_captured);
        onButtonDown(mouse_button);
    }
}

void updateViewport(Viewport *viewport, Mouse *mouse) {
    if (mouse->is_captured) {
        navigateViewport(viewport, app->time.timers.update.delta_time);
        if (mouse->moved)         orientViewport(viewport, mouse);
        if (mouse->wheel_scrolled)  zoomViewport(viewport, mouse);
    } else {
        if (mouse->wheel_scrolled) {
            if (app->controls.is_pressed.alt)
                zoomViewport(&secondary_viewport, mouse);
            else
                dollyViewport(viewport, mouse);
        }
        if (mouse->moved) {
            if (mouse->middle_button.is_pressed)  panViewport(viewport, mouse);
            if (mouse->right_button.is_pressed) orbitViewport(viewport, mouse);
        }
    }
}

void renderIntro(Viewport *viewport, f32 delta_time, f32 elapsed_time) {
    rotatePrimitive(main_box_prim, delta_time / -3, 0, 0);

    drawGrid(viewport, Color(main_grid_prim->color), 1, main_grid, main_grid_prim,0);
    drawGrid(viewport, Color(projection_plane_prim->color), 1, projection_plane_grid, projection_plane_prim, 0);
    drawBox(viewport,  Color(main_box_prim->color), 1, main_box, main_box_prim, BOX__ALL_SIDES, 1);

    Edge edge;
    initBox(&projected_box);
    for (u8 i = 0; i < BOX__VERTEX_COUNT; i++) {
        edge.from = app->scene.cameras[1].transform.position;
        edge.to = convertPositionToWorldSpace(main_box->vertices.buffer[i], main_box_prim);

        transformEdge(&edge, &view_space_edge, &secondary_viewport.camera->transform);
        edge.from = scaleVec3(view_space_edge.to, secondary_viewport.settings.near_clipping_plane_distance/view_space_edge.to.z);
        edge.from = mulVec3Mat3(edge.from, secondary_viewport.camera->transform.rotation_matrix);
        edge.from = addVec3(edge.from, secondary_viewport.camera->transform.position);

        transformEdge(&edge, &view_space_edge, &viewport->camera->transform);
        projected_box.vertices.buffer[i] = view_space_edge.from;

        drawEdgeF(viewport, Color(Magenta), 0.5f, &view_space_edge, 1);
    }

    setBoxEdgesFromVertices(&projected_box.edges, &projected_box.vertices);
    for (u8 i = 0; i < BOX__EDGE_COUNT; i++)
        drawEdge3D(viewport, Color(Yellow), 0.5f, projected_box.edges.buffer + i, 1);
}

void renderViewFrustum(Viewport *viewport, f32 delta_time, f32 elapsed_time) {
    drawArrow(viewport, Color(BrightRed),   1, &arrowX, 2);
    drawArrow(viewport, Color(BrightGreen), 1, &arrowY, 2);
    drawArrow(viewport, Color(BrightBlue),  1, &arrowZ, 2);

    mat4 M = getMat4Identity();

    // Transform vertices positions from local-space to world-space and then to view-space:
    BoxVertices vertices;
    transformBoxVerticesFromObjectToViewSpace(viewport, secondary_camera_prim, &NDC_box.vertices, &vertices);

    // Distribute transformed vertices positions to edges:
    BoxEdges edges;
    setBoxEdgesFromVertices(&edges, &vertices);
    for (u8 i = 0; i < BOX__EDGE_COUNT; i++)
        drawEdgeF(viewport, Color(Yellow), 1, edges.buffer + i, 1);

    view_frustum_box.vertices.corners = getViewFrustumCorners(&secondary_viewport);
    setBoxEdgesFromVertices(&view_frustum_box.edges, &view_frustum_box.vertices);
    transforming_view_frustum_box = view_frustum_box;

    if (transitions.full_projection.active) {
        if (incTransition(&transitions.full_projection, delta_time, true)) {
            transitionBox(&transitions.full_projection, &view_frustum_box, &NDC_box);
            sides_color = getColorInBetween(sides_color, Color(Yellow), transitions.full_projection.eased_t);
            near_color  = getColorInBetween(near_color,  Color(Yellow), transitions.full_projection.eased_t);
            far_color   = getColorInBetween(far_color,   Color(Yellow), transitions.full_projection.eased_t);
        }
    }
    drawFrustum(viewport, &transforming_view_frustum_box, near_color, far_color, sides_color, 1, 1);

    edge_color = Color(DarkYellow);
    drawBox(viewport, edge_color, 1, main_box, main_box_prim, BOX__ALL_SIDES, 0);

    Edge *clipped_edge;
    transformGridVerticesFromObjectToViewSpace(&secondary_viewport, main_grid_prim, main_grid, &clipped_grid.vertices);
    setGridEdgesFromVertices(clipped_grid.edges.uv.u, main_grid->u_segments, clipped_grid.vertices.uv.u.from, clipped_grid.vertices.uv.u.to);
    setGridEdgesFromVertices(clipped_grid.edges.uv.v, main_grid->v_segments, clipped_grid.vertices.uv.v.from, clipped_grid.vertices.uv.v.to);

    edge_color = Color(main_grid_prim->color);
    clipped_edge = clipped_grid.edges.uv.u;
    for (u8 u = 0; u < main_grid->u_segments; u++, clipped_edge++) {
        if (cullAndClipEdge(clipped_edge, &secondary_viewport)) {
            drawClippedEdge(viewport, transformedEdge(clipped_edge, M, M), edge_color, 1);
        }
    }

    clipped_edge = clipped_grid.edges.uv.v;
    for (u8 v = 0; v < main_grid->v_segments; v++, clipped_edge++) {
        if (cullAndClipEdge(clipped_edge, &secondary_viewport)) {
            drawClippedEdge(viewport, transformedEdge(clipped_edge, M, M), edge_color, 1);
        }
    }

    transformBoxVerticesFromObjectToViewSpace(&secondary_viewport, main_box_prim, &main_box->vertices, &clipped_box.vertices);
    setBoxEdgesFromVertices(&clipped_box.edges, &clipped_box.vertices);
    clipped_edge = clipped_box.edges.buffer;
    edge_color = Color(main_box_prim->color);
    for (u8 i = 0; i < BOX__EDGE_COUNT; i++, clipped_edge++) {
        if (cullAndClipEdge(clipped_edge, &secondary_viewport)) {
            drawClippedEdge(viewport, transformedEdge(clipped_edge, M, M), edge_color, 1);
        }
    }
}

void renderProjection(Viewport *viewport, f32 delta_time) {
    drawArrow(viewport, Color(BrightRed),   1, &arrowX, 2);
    drawArrow(viewport, Color(BrightGreen), 1, &arrowY, 2);
    drawArrow(viewport, Color(BrightBlue),  1, &arrowZ, 2);

    view_frustum_box.vertices.corners = getViewFrustumCorners(&secondary_viewport);
    setBoxEdgesFromVertices(&view_frustum_box.edges, &view_frustum_box.vertices);
    transforming_view_frustum_box = view_frustum_box;
    drawFrustum(viewport, &transforming_view_frustum_box, near_color, far_color, sides_color, 1, 1);
    drawBox(viewport, Color(Grey), 1, main_box, main_box_prim, BOX__ALL_SIDES, 0);

    vec3 side_col, far_col, near_col, color, out_color = Color(BrightGrey);
    if (show_pre_projection) {
        drawBox(viewport, Color(Magenta), 1, &pre_projected_view_frustum_box, secondary_camera_prim, BOX__ALL_SIDES, 0);

        if (transitions.pre_projection.active) {
            if (incTransition(&transitions.pre_projection, delta_time, true)) {
                transitionBox(&transitions.pre_projection, &view_frustum_box, &pre_projected_view_frustum_box);
                side_col = getColorInBetween(sides_color, Color(Magenta), transitions.pre_projection.eased_t);
                near_col = getColorInBetween(near_color,  Color(Magenta), transitions.pre_projection.eased_t);
                far_col  = getColorInBetween(far_color,   Color(Magenta), transitions.pre_projection.eased_t);
                drawFrustum(viewport, &transforming_view_frustum_box, near_col, far_col, side_col, 1, 1);

                if (draw_locator_grids)
                    drawLocatorGrid(viewport, Color(Yellow), out_color, LOCATOR_OPACITY, &transitions.pre_projection);
            } else {
                transitions.projection.active = true;
                transitions.projection.t = 0;
            }
        }

        if (transitions.projection.active) {
            if (incTransition(&transitions.projection, delta_time, true)) {
                transitionBox(&transitions.projection, &pre_projected_view_frustum_box, &NDC_box);
                color = getColorInBetween(Color(Magenta), Color(Yellow), transitions.projection.eased_t);
                drawFrustum(viewport, &transforming_view_frustum_box, color, color, color, 1, 1);
                if (draw_locator_grids)
                    drawLocatorGrid(viewport, color, out_color, LOCATOR_OPACITY, &transitions.projection);
            }
        }
    }

    if (transitions.full_projection.active) {
        if (incTransition(&transitions.full_projection, delta_time, true)) {
            transitionBox(&transitions.full_projection, &view_frustum_box, &NDC_box);
            side_col = getColorInBetween(sides_color, Color(Yellow), transitions.full_projection.eased_t);
            near_col = getColorInBetween(near_color,  Color(Yellow), transitions.full_projection.eased_t);
            far_col  = getColorInBetween(far_color,   Color(Yellow), transitions.full_projection.eased_t);
            drawFrustum(viewport, &transforming_view_frustum_box, near_col, far_col, side_col, 1, 1);
            if (draw_locator_grids)
                drawLocatorGrid(viewport, color, out_color, LOCATOR_OPACITY, &transitions.full_projection);
        }
    }

    BoxVertices vertices;
    transformBoxVerticesFromObjectToViewSpace(viewport, secondary_camera_prim, &NDC_box.vertices, &vertices);
    BoxEdges edges;
    setBoxEdgesFromVertices(&edges, &vertices);
    for (u8 i = 0; i < BOX__EDGE_COUNT; i++)
        drawEdgeF(viewport, Color(Yellow), 1, edges.buffer + i, 1);
}

void renderProjectiveSpace(Viewport *viewport, f32 delta_time) {
    drawGrid(viewport, Color(Magenta), 1, projective_ref_plane_grid, projective_ref_plane_prim,0);
    drawBox(viewport, Color(Grey), 1, main_box, main_box_prim, BOX__ALL_SIDES, 0);

    if (transitions.projective_lines.active) {
        if (incTransition(&transitions.projective_lines, delta_time, false))
            drawProjectiveSpace(viewport, &transitions.projective_lines, true);
    }

    viewport->settings.depth_sort = false;
    updateCameraArrows(&secondary_viewport.camera->transform);
    drawArrow(viewport, X_color, 1, &arrowX, 2);
    drawArrow(viewport, Y_color, 1, &arrowY, 2);
    drawArrow(viewport, Z_color, 1, &arrowZ, 2);
    viewport->settings.depth_sort = true;
}
#define TEXT_OVERLAY_BACKGROUND_PADDING 5

void updateAndRender() {
    Timer *timer = &app->time.timers.update;
    Mouse *mouse = &app->controls.mouse;
    Viewport *viewport = &app->viewport;

    startFrameTimer(timer);
    fillPixelGrid(viewport->frame_buffer, Color(Black), 1);

    static float elapsed = 0;
    elapsed += timer->delta_time;

    if (app->controls.is_pressed.ctrl) {
        if (mouse->wheel_scrolled) {
            f32 z = secondary_viewport.navigation.settings.speeds.zoom * mouse->wheel_scroll_amount;
            secondary_viewport.settings.near_clipping_plane_distance += z;
            mouse->wheel_scroll_handled = true;
        }
    } else if (app->controls.is_pressed.shift) {
        if (mouse->wheel_scrolled) {
            f32 z = secondary_viewport.navigation.settings.speeds.zoom * mouse->wheel_scroll_amount;
            secondary_viewport.settings.far_clipping_plane_distance += z;
            mouse->wheel_scroll_handled = true;
        }
    } else updateViewport(active_viewport, mouse);

    if (active_viewport->navigation.moved ||
        active_viewport->navigation.turned) {
        if (active_viewport == &secondary_viewport)
            updateCameraArrows(&secondary_viewport.camera->transform);
    }

    if (!mouse->is_captured) {
        // Temporarily override primitive count so that only the first 3 primitives (arrows) can be manipulated:
        u32 primitive_count = app->scene.settings.primitives;
        app->scene.settings.primitives = 3;
        vec3 original_positions[3] = {
                app->scene.primitives[0].position,
                app->scene.primitives[1].position,
                app->scene.primitives[2].position};
        manipulateSelection(&app->scene, active_viewport, &app->controls);
        if (transitions.view_frustom_slice.active && app->scene.selection->primitive == 0)
            app->scene.primitives[0].position = original_positions[0];

        app->scene.settings.primitives = primitive_count;
        if (app->controls.is_pressed.shift) {
            if (app->controls.is_pressed.ctrl) {
                for (u8 i = 0; i < 3; i++) {
                    app->scene.primitives[i].position.y = original_positions[i].y;
                    app->scene.primitives[i].position.z = original_positions[i].z;
                }
            } else {
                for (u8 i = 0; i < 3; i++) {
                    app->scene.primitives[i].position.x = original_positions[i].x;
                    app->scene.primitives[i].position.z = original_positions[i].z;
                }
            }
        } else if (app->controls.is_pressed.ctrl) {
            for (u8 i = 0; i < 3; i++) {
                app->scene.primitives[i].position.x = original_positions[i].x;
                app->scene.primitives[i].position.y = original_positions[i].y;
            }
        }
    }

    setPreProjectionMatrix(viewport);
    setPreProjectionMatrix(&secondary_viewport);

    if (orbit) {
        f32 target_distance = viewport->camera->target_distance;
        viewport->camera->target_distance = lengthVec3(viewport->camera->transform.position);
        orbitCamera(viewport->camera, timer->delta_time / 30, 0);
        viewport->camera->target_distance = target_distance;
    }

    viewport->settings.depth_sort = true;
    drawGrid(viewport, Color(DarkGrey), 1, main_grid, main_grid_prim,0);

    secondary_camera_prim->position = secondary_viewport.camera->transform.position;
    secondary_camera_prim->rotation = secondary_viewport.camera->transform.rotation;
    main_camera_prim->position = viewport->camera->transform.position;
    main_camera_prim->rotation = viewport->camera->transform.rotation;
    updateProjectionBoxes(&secondary_viewport);

    sides_color = Color(Cyan);
    near_color = default_near_color;
    far_color = default_far_color;
    NDC_color = Color(Yellow);

    X_color = Color(BrightRed);
    Y_color = Color(BrightGreen);
    Z_color = Color(BrightBlue);
    W_color = Color(Magenta);

    if (current_viz != VIEW_FRUSTUM_SLICE)
        drawCamera(viewport, camera_color, camera_opacity, secondary_viewport.camera, 0);

    labels.count = 0;

    switch (current_viz) {
        case INTRO:              renderIntro(viewport, timer->delta_time, elapsed); break;
        case VIEW_FRUSTUM:       renderViewFrustum(viewport, timer->delta_time, elapsed); break;
        case PROJECTION:         renderProjection(viewport, timer->delta_time); break;
        case PROJECTIVE_SPACE:   renderProjectiveSpace(viewport, timer->delta_time); break;
        case VIEW_FRUSTUM_SLICE: renderViewSpaceFrustumSlice(viewport, timer->delta_time); break;
    }

    Label *label;
    if (labels.count) {
        vec3 background_color = Color(Black);

        text_overlay_frame_buffer.dimensions = viewport->frame_buffer->dimensions;
        fillPixelGrid(&text_overlay_frame_buffer, background_color, 0);
        background_color = Color(Black);

        label = labels.array;
        for (u8 i = 0; i < labels.count; i++, label++) {
            if (label->text[0] == 'X' ||
                label->text[0] == 'Y' ||
                label->text[0] == 'Z' ||
                label->text[0] == 'W')
                continue;

            Rect rect;
            rect.min = rect.max = label->position;
            rect.max.x += FONT_WIDTH * (i32)getStringLength(label->text);
            rect.max.y += FONT_HEIGHT;
            rect.min.x -= TEXT_OVERLAY_BACKGROUND_PADDING;
            rect.min.y -= TEXT_OVERLAY_BACKGROUND_PADDING;
            rect.max.x += TEXT_OVERLAY_BACKGROUND_PADDING;
            rect.max.y += TEXT_OVERLAY_BACKGROUND_PADDING;
            fillRectF(&text_overlay_frame_buffer, background_color, 0.85f, &rect);
        }
        FloatPixel *float_pixel = text_overlay_frame_buffer.float_pixels;
        for (i32 y = 0; y < viewport->frame_buffer->dimensions.height; y++)
            for (i32 x = 0; x < viewport->frame_buffer->dimensions.width; x++, float_pixel++)
                if (float_pixel->opacity)
                    setPixel(viewport->frame_buffer, float_pixel->color, float_pixel->opacity, x, y, 0);
    }

    if (app->viewport.settings.show_hud && current_viz == VIEW_FRUSTUM_SLICE) {
        updateMatrixStrings(&main_matrix);
        if (matrix_count) {
            Matrix *matrix = matrices;
            for (u8 i = 0; i < matrix_count; i++, matrix++) {
                matrix->dim = transitions.view_frustom_slice.active ? 4 : 3;
                setMatrixComponentColor(matrix);
            }
        }
        main_matrix.dim = transitions.view_frustom_slice.active ? 4 : 3;
        setMatrixComponentColor(&main_matrix);

        if (secondary_viewport.settings.use_cube_NDC) {
            copyToString(&final_matrix.components[0][0].string, "L/A", 0);
            copyToString(&final_matrix.components[1][1].string, "L", 0);
            copyToString(&final_matrix.components[2][3].string, secondary_viewport.settings.flip_z ? "-1" : "1", 0);
            copyToString(&final_matrix.components[3][3].string, "0", 0);
            copyToString(&final_matrix.components[3][2].string, "-2NF/(F-N)", 0);
            copyToString(&final_matrix.components[2][2].string, (secondary_viewport.settings.flip_z ? "-(F+N)/(F-N)" : "(F+N)/(F-N)"), 0);
            final_matrix.component_colors[0][0] = ColorOf(White);
            final_matrix.component_colors[1][1] = ColorOf(White);
            final_matrix.component_colors[2][2] = ColorOf(White);
            final_matrix.component_colors[2][3] = ColorOf(White);
            final_matrix.component_colors[3][2] = ColorOf(White);
            final_matrix.component_colors[3][3] = ColorOf(White);
        } else {
            copyToString(&final_matrix.components[0][0].string, "L/A", 0);
            copyToString(&final_matrix.components[1][1].string, "L", 0);
            copyToString(&final_matrix.components[2][3].string, "1", 0);
            copyToString(&final_matrix.components[3][3].string, "0", 0);
            copyToString(&final_matrix.components[3][2].string, "-NF/(F-N)", 0);
            copyToString(&final_matrix.components[2][2].string, "F/(F-N)", 0);
            final_matrix.component_colors[0][0] = ColorOf(White);
            final_matrix.component_colors[1][1] = ColorOf(White);
            final_matrix.component_colors[2][2] = ColorOf(White);
            final_matrix.component_colors[3][2] = ColorOf(White);
        }

        Rect rect;
        rect.min.x = rect.min.y = 0;
        rect.max.x = viewport->frame_buffer->dimensions.width;
        rect.max.y = MATRIX_HUD_START_Y + LINE_HEIGHT * 2 + (i32)getMatrixHeight(show_final_matrix ? &final_matrix : &main_matrix) + MATRIX_PADDING;
        fillRectF(viewport->frame_buffer, Color(Black), 0.85f, &rect);

        preparePixelGridForDisplay(viewport->frame_buffer);
    }

    preparePixelGridForDisplay(viewport->frame_buffer);

    if (labels.count) {
        label = labels.array;
        for (u8 i = 0; i < labels.count; i++, label++)
            drawText(viewport->frame_buffer, label->color, label->text, label->position.x, label->position.y);
    }

    if (app->viewport.settings.show_hud) {
        if (current_viz == VIEW_FRUSTUM_SLICE)
            drawMatrixHUD(viewport->frame_buffer, transitions.focal_length_and_plane.active, transitions.aspect_ratio.active);
        else {
            fillPixelGrid(&secondary_viewport_frame_buffer, Color(Black), 1);

            drawGrid(&secondary_viewport, Color(main_grid_prim->color), 1, main_grid, main_grid_prim,0);
            drawBox(&secondary_viewport, Color(main_box_prim->color), 1, main_box, main_box_prim, BOX__ALL_SIDES, 1);

            preparePixelGridForDisplay(&secondary_viewport_frame_buffer);
            if (labels.count) {
                label = labels.array;
                for (u8 i = 0; i < labels.count; i++, label++)
                    drawText(viewport->frame_buffer, label->color, label->text, label->position.x, label->position.y);
            }
            drawSecondaryViewportToFrameBuffer(viewport->frame_buffer);
        }
    }

    active_viewport->navigation.moved  = false;
    active_viewport->navigation.turned = false;
    resetMouseChanges(mouse);
    endFrameTimer(timer);
}

void setupViewportCore(Viewport *viewport) {
    Scene *scene = &app->scene;
    arrowX.label = arrowY.label = arrowZ.label = null;

    for (u32 i = 0; i < scene->settings.cameras; i++) initCamera(scene->cameras + i);
    for (u32 i = 0; i < scene->settings.primitives; i++) { initPrimitive(scene->primitives + i); scene->primitives[i].id = i; }
    for (u32 i = 0; i < scene->settings.boxes; i++) initBox(scene->boxes + i);
    for (u32 i = 0; i < scene->settings.grids; i++) initGrid(scene->grids + i, 3, 3);

    Primitive *primitive = scene->primitives;
    arrowX_box_prim = primitive++;
    arrowY_box_prim = primitive++;
    arrowZ_box_prim = primitive++;
    main_box_prim   = primitive++;
    main_grid_prim  = primitive++;
    projection_plane_prim     = primitive++;
    projective_ref_plane_prim = primitive++;
    main_camera_prim          = primitive++;
    secondary_camera_prim     = primitive;

    main_box_prim->type = arrowX_box_prim->type = arrowY_box_prim->type = arrowZ_box_prim->type = PrimitiveType_Box;
    main_grid_prim->type = projection_plane_prim->type = projective_ref_plane_prim->type = PrimitiveType_Grid;

    main_box = scene->boxes;

    Grid *grid = scene->grids;
    main_grid = grid++;
    projection_plane_grid = grid++;
    projective_ref_plane_grid = grid;

    main_box_prim->color = Yellow;
    main_grid_prim->color = White;
    projection_plane_prim->color = Cyan;
    camera_color = Color(projection_plane_prim->color);

    xform3 *camera_xform = &scene->cameras[0].transform;
    camera_xform->position = Vec3(0, 13, -30);
    rotateXform3(camera_xform, 0, -0.15f, 0);

    draw_locator_grids = false;
    show_pre_projection = false;
    move_projective_point = true;

    camera_xform = &scene->cameras[1].transform;

    if (current_viz == VIEW_FRUSTUM_SLICE) {
        initGrid(main_grid,41, 41);
        main_grid_prim->scale    = Vec3(20, 20, 20);
        transformed_grid = *main_grid;

        initGrid(projective_ref_plane_grid,41, 41);
        projective_ref_plane_prim->color = Magenta;
        projective_ref_plane_prim->scale    = Vec3(20, 1, 20);
        projective_ref_plane_prim->position = Vec3(0, 1, 0);

        main_box_prim->position = Vec3(0, 1, 7);
        rotatePrimitive(main_box_prim, 0.23f, 0, 0);

        arrowX_box_prim->position = Vec3(1, 0, 0);
        arrowY_box_prim->position = Vec3(0, 1, 0);
        arrowZ_box_prim->position = Vec3(0, 0, 1);
        arrowX_box_prim->scale = Vec3(0.2f, 0.2f, 0.2f);
        arrowY_box_prim->scale = Vec3(0.2f, 0.2f, 0.2f);
        arrowZ_box_prim->scale = Vec3(0.2f, 0.2f, 0.2f);
    } else {
        rotatePrimitive(main_grid_prim, 0.5f, 0, 0);
        camera_xform->position = Vec3(0, 9, -10);
        rotateXform3(camera_xform, 0, -0.2f, 0);

        initGrid(main_grid,11, 11);
        main_grid_prim->scale    = Vec3(5, 1, 5);
        main_grid_prim->position = Vec3(0, 0, 5);

        vec2 sin_cos = Vec2(SQRT2_OVER_2, SQRT2_OVER_2);
        vec3 x_axis = Vec3(1, 0, 0);

        projection_plane_prim->id = 1;
        projection_plane_prim->scale = Vec3(6, 1, 4);
        projection_plane_prim->position = scaleAddVec3(*camera_xform->forward_direction,
                                                       secondary_viewport.settings.near_clipping_plane_distance,
                                                       camera_xform->position);
        initGrid(projection_plane_grid, 2, 2);
        projection_plane_prim->rotation = rotateAroundAxisBySinCos(camera_xform->rotation, x_axis, sin_cos);

        projective_ref_plane_prim->id = 2;
        //    projective_ref_plane_prim->scale = Vec3(10, 1, 10);
        projective_ref_plane_prim->position = scaleAddVec3(*camera_xform->forward_direction, 1, camera_xform->position);
        initGrid(projective_ref_plane_grid, 11, 11);
        projective_ref_plane_prim->rotation = rotateAroundAxisBySinCos(camera_xform->rotation, x_axis, sin_cos);

        main_box_prim->position = Vec3(0, 3, 5);
        main_box_prim->scale = getVec3Of(2);
    }

    arrow1.body.from = Vec3(2, 0, 3);
    arrow1.body.to = addVec3(arrow1.body.from, getVec3Of(10));
    arrow1.head.length = 1;
    updateArrow(&arrow1);

    scene->selection->primitive = scene->primitives + 1;

    initBox(&NDC_box);
    if (!secondary_viewport.settings.use_cube_NDC) for (u8 i = 4; i < 8; i++) NDC_box.vertices.buffer[i].z = 0;
    setBoxEdgesFromVertices(&NDC_box.edges, &NDC_box.vertices);

    secondary_camera_prim->position = secondary_viewport.camera->transform.position;
    secondary_camera_prim->rotation = secondary_viewport.camera->transform.rotation;
    main_camera_prim->position = viewport->camera->transform.position;
    main_camera_prim->rotation = viewport->camera->transform.rotation;

    updateProjectionBoxes(&secondary_viewport);
    updateCameraArrows(&secondary_viewport.camera->transform);

    for (u8 i = 0; i < TRANSITION_COUNT; i++) {
        transitions.states[i].active = false;
        transitions.states[i].t = 0;
        transitions.states[i].speed = i == 2 ? 0.25f : (i == 3 ? PROJECTION_LINE_INC_SPEED : 0.5f);
    }
    viewport->camera->target_distance = lengthVec3(viewport->camera->transform.position);
}

void onKeyChanged(u8 key, bool is_pressed) {
    NavigationMove *move = &active_viewport->navigation.move;
    NavigationTurn *turn = &active_viewport->navigation.turn;
    if (key == 'Q') turn->left     = is_pressed;
    if (key == 'E') turn->right    = is_pressed;
    if (key == 'R') move->up       = is_pressed;
    if (key == 'F') move->down     = is_pressed;
    if (key == 'W') move->forward  = is_pressed;
    if (key == 'S') move->backward = is_pressed;
    if (key == 'A') move->left     = is_pressed;
    if (key == 'D') move->right    = is_pressed;

    if (!is_pressed) {
        if (key == app->controls.key_map.space) {
            current_viz = (VIZ)(((u8)(current_viz) + 1) % (u8)VIS_COUNT);
            setupViewportCore(&app->viewport);
        } else if (key == app->controls.key_map.tab)
            app->viewport.settings.show_hud = !app->viewport.settings.show_hud;
        else if (key == 'R')
            orbit = !orbit;
        else if (key == 'Q')
            updateDimensions(&secondary_viewport_frame_buffer.dimensions,
                             secondary_viewport_frame_buffer.dimensions.width - 50,
                             secondary_viewport_frame_buffer.dimensions.height);
        else if (key == 'E')
            updateDimensions(&secondary_viewport_frame_buffer.dimensions,
                             secondary_viewport_frame_buffer.dimensions.width + 50,
                             secondary_viewport_frame_buffer.dimensions.height);
        else updateTransitions(key);
    }
}

void setupViewport(Viewport *viewport) {
    active_viewport = viewport;
    viewport->settings.antialias = viewport->settings.depth_sort = true;
    initPixelGrid(&secondary_viewport_frame_buffer, allocateAppMemory(RENDER_SIZE), MAX_WIDTH, MAX_HEIGHT);
    initPixelGrid(&text_overlay_frame_buffer, allocateAppMemory(RENDER_SIZE), MAX_WIDTH, MAX_HEIGHT);
    initViewport(&secondary_viewport,
                 &viewport->settings,
                 &viewport->navigation.settings,
                 app->scene.cameras + 1,
                 &secondary_viewport_frame_buffer);
    updateDimensions(&secondary_viewport_frame_buffer.dimensions, 600,400);
    secondary_viewport.settings.position.x = 20;
    secondary_viewport.settings.position.y = 20;
    secondary_viewport.settings.far_clipping_plane_distance = 20;
    secondary_viewport.settings.near_clipping_plane_distance = 4;

    initMatrix(&main_matrix);
    initMatrix(&final_matrix);
    for (u8 i = 0; i < MAX_MATRIX_COUNT; i++)
        initMatrix(matrices + i);

    default_near_color = Color(BrightRed);
    default_far_color = Color(BrightGreen);
    default_aspect_ratio_color = Color(BrightMagenta);
    default_focal_length_color = Color(BrightCyan);

    setupViewportCore(viewport);
}

void initApp(Defaults *defaults) {
    app->on.viewportReady = setupViewport;
    app->on.windowRedraw  = updateAndRender;
    app->on.keyChanged               = onKeyChanged;
    app->on.mouseButtonDown          = onButtonDown;
    app->on.mouseButtonDoubleClicked = onDoubleClick;
    defaults->settings.viewport.hud_default_color = Green;
    defaults->settings.scene.grids      = 3;
    defaults->settings.scene.boxes      = 1;
    defaults->settings.scene.primitives = 10;
    defaults->settings.scene.cameras    = 2;
    defaults->additional_memory_size = 2 * RENDER_SIZE;
}