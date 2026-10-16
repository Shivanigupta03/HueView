// Contact form, delivered by FormSubmit.co (no account needed).
const FORM_URL = "https://formsubmit.co/ajax/agar.vedika@gmail.com";

const dlg = document.createElement("dialog");
dlg.className = "contact";
dlg.innerHTML = `
<form method="dialog" class="cf-close"><button aria-label="Close">×</button></form>
<h3>Say hi</h3>
<p class="muted">Questions, collaborations, or want to try the tests? Send me a message.</p>
<form id="cf" novalidate>
  <label>Your name<input name="name" required autocomplete="name"></label>
  <label>Your email<input name="email" type="email" required autocomplete="email"></label>
  <label>Message<textarea name="message" rows="5" required></textarea></label>
  <input type="hidden" name="_subject" value="New message from the HueBelieve website">
  <input type="hidden" name="_template" value="table">
  <input name="_honey" tabindex="-1" autocomplete="off" style="display:none">
  <button class="btn primary" type="submit">Send message</button>
  <p id="cf-status" role="status"></p>
</form>`;
document.body.appendChild(dlg);

const form = dlg.querySelector("#cf");
const status = dlg.querySelector("#cf-status");

document.addEventListener("click", (e) => {
  const t = e.target.closest("[data-contact]");
  if (!t) return;
  e.preventDefault();
  status.textContent = "";
  dlg.showModal();
});
dlg.addEventListener("click", (e) => { if (e.target === dlg) dlg.close(); });

form.addEventListener("submit", async (e) => {
  e.preventDefault();
  if (!form.reportValidity()) return;
  const btn = form.querySelector("button[type=submit]");
  btn.disabled = true;
  status.textContent = "Sending…";
  try {
    const r = await fetch(FORM_URL, {
      method: "POST",
      headers: { Accept: "application/json" },
      body: new FormData(form),
    });
    if (!r.ok) throw new Error();
    form.reset();
    status.textContent = "Thanks! Your message has been sent.";
  } catch {
    status.textContent = "Something went wrong. Please try again.";
  }
  btn.disabled = false;
});
