// Counts downloads in Supabase. Fill in the two values below (Supabase > Project Settings > API).
const SUPABASE_URL = "https://klqikgeophmbifkekarr.supabase.co";
const SUPABASE_ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImtscWlrZ2VvcGhtYmlma2VrYXJyIiwicm9sZSI6ImFub24iLCJpYXQiOjE3OTIxMzU0MDcsImV4cCI6MjEwNzcxMTQwN30.4Kw-sSeBF3WzOmPADetAJaGHq1MS1VWiWo6PlwrupdI";

document.addEventListener("click", (e) => {
  const a = e.target.closest("a[download]");
  if (!a) return;
  fetch(SUPABASE_URL + "/rest/v1/rpc/increment_download", {
    method: "POST",
    keepalive: true,
    headers: {
      "Content-Type": "application/json",
      apikey: SUPABASE_ANON_KEY,
      Authorization: "Bearer " + SUPABASE_ANON_KEY,
    },
    body: JSON.stringify({ p_file_path: a.getAttribute("href") }),
  }).catch(() => {});
});
