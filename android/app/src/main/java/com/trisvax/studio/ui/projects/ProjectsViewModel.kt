package com.trisvax.studio.ui.projects

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.trisvax.studio.TrisvaxApplication
import com.trisvax.studio.models.ProjectSummaryDto
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json

data class ProjectsUiState(
    val projects: List<ProjectSummaryDto> = emptyList(),
    val filteredProjects: List<ProjectSummaryDto> = emptyList(),
    val searchQuery: String = "",
    val isSearchActive: Boolean = false,
    val totalCount: Int = 0,
    val totalStorageBytes: Long = 0L
)

class ProjectsViewModel(application: Application) : AndroidViewModel(application) {
    private val app = application as TrisvaxApplication
    private val bridge = app.nativeBridge
    private val json = Json { ignoreUnknownKeys = true }

    private val _uiState = MutableStateFlow(ProjectsUiState())
    val uiState: StateFlow<ProjectsUiState> = _uiState.asStateFlow()

    init {
        refresh()
    }

    fun refresh() {
        viewModelScope.launch {
            val list = withContext(Dispatchers.IO) {
                runCatching {
                    json.decodeFromString<List<ProjectSummaryDto>>(bridge.listProjectsJson())
                }.getOrDefault(emptyList())
            }
            _uiState.update { state ->
                val filtered = filterList(list, state.searchQuery)
                state.copy(
                    projects = list,
                    filteredProjects = filtered,
                    totalCount = list.size,
                    totalStorageBytes = list.sumOf { it.storageSizeBytes }
                )
            }
        }
    }

    fun toggleSearch(active: Boolean) {
        _uiState.update { state ->
            val q = if (active) state.searchQuery else ""
            state.copy(
                isSearchActive = active,
                searchQuery = q,
                filteredProjects = filterList(state.projects, q)
            )
        }
    }

    fun updateSearchQuery(query: String) {
        _uiState.update { state ->
            state.copy(
                searchQuery = query,
                filteredProjects = filterList(state.projects, query)
            )
        }
    }

    fun renameProject(projectId: String, newName: String) {
        viewModelScope.launch {
            withContext(Dispatchers.IO) { bridge.renameProject(projectId, newName) }
            refresh()
        }
    }

    fun deleteProject(projectId: String) {
        viewModelScope.launch {
            withContext(Dispatchers.IO) { bridge.deleteProject(projectId) }
            refresh()
        }
    }

    fun createProjectFromUris(uris: List<Uri>, onCreated: (String) -> Unit) {
        if (uris.isEmpty()) return
        viewModelScope.launch {
            val pid = withContext(Dispatchers.IO) {
                val createdId = bridge.createProject("New project")
                if (createdId.isBlank()) return@withContext ""
                uris.forEachIndexed { idx, uri ->
                    val resolved = app.mediaResolver.resolve(createdId, uri, idx)
                    if (resolved != null) {
                        bridge.importMedia(
                            resolved.uriString,
                            resolved.displayName,
                            resolved.isImage,
                            resolved.durationUs,
                            resolved.width,
                            resolved.height,
                            resolved.fileSizeBytes,
                            resolved.thumbnailPath
                        )
                    }
                }
                bridge.saveProject()
                createdId
            }
            refresh()
            if (pid.isNotBlank()) onCreated(pid)
        }
    }

    private fun filterList(list: List<ProjectSummaryDto>, query: String): List<ProjectSummaryDto> {
        val q = query.trim()
        if (q.isEmpty()) return list
        return list.filter { it.name.contains(q, ignoreCase = true) }
    }
}
